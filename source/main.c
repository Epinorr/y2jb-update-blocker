#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sqlite3.h"
#include "cJSON.h"

#define TARGET_VERSION "99.999.999"
#define TARGET_URI     "http://127.0.0.2"
#define MAX_JSON_BYTES (8 * 1024 * 1024)

/* Set only by the native test harness. Empty on the PS5 build. */
#ifndef YT_BLOCKER_TEST_ROOT
#define YT_BLOCKER_TEST_ROOT ""
#endif

static const char *const TARGET_TITLE_IDS[] = {
    "PPSA01650",
    "PPSA01651",
    "PPSA01652"
};

#define TARGET_TITLE_COUNT (sizeof(TARGET_TITLE_IDS) / sizeof(TARGET_TITLE_IDS[0]))

enum stage_status {
    STAGE_ERROR = -1,
    STAGE_MISSING = 0,
    STAGE_OK = 1
};

typedef struct stage_result {
    int status;
    int present;
    int rows;
    char detail[192];
} stage_result_t;

typedef struct title_result {
    stage_result_t appinfo;
    stage_result_t appdb;
    stage_result_t system_param;
    stage_result_t user_param;
} title_result_t;

#ifndef YT_BLOCKER_TEST_MODE
typedef struct notify_request {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
#endif

static int notify_message(const char *fmt, ...)
{
    va_list args;
#ifdef YT_BLOCKER_TEST_MODE
    va_start(args, fmt);
    vprintf(fmt, args);
    putchar('\n');
    va_end(args);
    return 0;
#else
    notify_request_t request;
    int rc;

    memset(&request, 0, sizeof(request));
    va_start(args, fmt);
    vsnprintf(request.message, sizeof(request.message), fmt, args);
    va_end(args);

    rc = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    return rc;
#endif
}

static void result_set(stage_result_t *result, int status, int present,
                       int rows, const char *fmt, ...)
{
    va_list args;
    result->status = status;
    result->present = present;
    result->rows = rows;
    result->detail[0] = '\0';
    if (fmt != NULL) {
        va_start(args, fmt);
        vsnprintf(result->detail, sizeof(result->detail), fmt, args);
        va_end(args);
    }
}

static void make_target_path(char *out, size_t out_size, const char *absolute_path)
{
    const char *root = YT_BLOCKER_TEST_ROOT;
    if (root[0] == '\0') {
        snprintf(out, out_size, "%s", absolute_path);
    } else {
        snprintf(out, out_size, "%s%s", root, absolute_path);
    }
}

static int open_database(const char *path, sqlite3 **db, char *error, size_t error_size)
{
    int rc = sqlite3_open_v2(path, db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        snprintf(error, error_size, "open %s failed: %s", path,
                 (*db != NULL) ? sqlite3_errmsg(*db) : sqlite3_errstr(rc));
        if (*db != NULL) {
            sqlite3_close(*db);
            *db = NULL;
        }
        return -1;
    }

    sqlite3_busy_timeout(*db, 5000);
    return 0;
}

static int db_exec_checked(sqlite3 *db, const char *sql, char *error, size_t error_size)
{
    char *sqlite_error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
    if (rc != SQLITE_OK) {
        snprintf(error, error_size, "%s", sqlite_error != NULL
                 ? sqlite_error : sqlite3_errmsg(db));
        if (sqlite_error != NULL) {
            sqlite3_free(sqlite_error);
        }
        return -1;
    }
    if (sqlite_error != NULL) {
        sqlite3_free(sqlite_error);
    }
    return 0;
}

/* Run a COUNT(*) statement, binding up to three consecutive text arguments. */
static int query_count(sqlite3 *db, const char *sql,
                       const char *arg1, const char *arg2, const char *arg3,
                       int *count, char *error, size_t error_size)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        return -1;
    }
    if (arg1 != NULL && sqlite3_bind_text(stmt, 1, arg1, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto bind_error;
    if (arg2 != NULL && sqlite3_bind_text(stmt, 2, arg2, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto bind_error;
    if (arg3 != NULL && sqlite3_bind_text(stmt, 3, arg3, -1, SQLITE_TRANSIENT) != SQLITE_OK) goto bind_error;

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    *count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;

bind_error:
    snprintf(error, error_size, "failed to bind query parameter: %s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return -1;
}

static int update_appinfo_value(sqlite3 *db, const char *title_id,
                                const char *key, const char *value,
                                int *rows, char *error, size_t error_size)
{
    static const char sql[] =
        "UPDATE tbl_appinfo SET val=?1 WHERE titleId=?2 AND key=?3";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, title_id, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, key, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        snprintf(error, error_size, "failed to bind update: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    *rows = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return 0;
}

static stage_result_t patch_appinfo_title(sqlite3 *db, const char *title_id)
{
    stage_result_t result;
    char error[192] = {0};
    int uri_count = 0, version_count = 0;
    int uri_rows = 0, version_rows = 0;
    int verified_uri = 0, verified_version = 0;

    result_set(&result, STAGE_MISSING, 0, 0, "no matching rows");
    if (db_exec_checked(db, "BEGIN IMMEDIATE", error, sizeof(error)) != 0) {
        result_set(&result, STAGE_ERROR, 0, 0, "begin transaction: %.150s", error);
        return result;
    }

    if (query_count(db,
                    "SELECT COUNT(*) FROM tbl_appinfo WHERE titleId=?1 AND key=?2",
                    title_id, "VERSION_FILE_URI", NULL, &uri_count,
                    error, sizeof(error)) != 0 ||
        query_count(db,
                    "SELECT COUNT(*) FROM tbl_appinfo WHERE titleId=?1 AND key=?2",
                    title_id, "CONTENT_VERSION", NULL, &version_count,
                    error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 0, 0, "check appinfo rows: %.150s", error);
        return result;
    }

    if (uri_count == 0 && version_count == 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        return result;
    }
    if (uri_count == 0 || version_count == 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, 0,
                   "incomplete appinfo records (URI rows=%d, version rows=%d)",
                   uri_count, version_count);
        return result;
    }

    if (update_appinfo_value(db, title_id, "VERSION_FILE_URI", TARGET_URI,
                             &uri_rows, error, sizeof(error)) != 0 ||
        update_appinfo_value(db, title_id, "CONTENT_VERSION", TARGET_VERSION,
                             &version_rows, error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, 0, "update appinfo: %.150s", error);
        return result;
    }

    if (uri_rows != uri_count || version_rows != version_count ||
        query_count(db,
                    "SELECT COUNT(*) FROM tbl_appinfo WHERE titleId=?1 AND key=?2 AND val=?3",
                    title_id, "VERSION_FILE_URI", TARGET_URI, &verified_uri,
                    error, sizeof(error)) != 0 ||
        query_count(db,
                    "SELECT COUNT(*) FROM tbl_appinfo WHERE titleId=?1 AND key=?2 AND val=?3",
                    title_id, "CONTENT_VERSION", TARGET_VERSION, &verified_version,
                    error, sizeof(error)) != 0 ||
        verified_uri != uri_count || verified_version != version_count) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, uri_rows + version_rows,
                   "appinfo verification failed (URI %d/%d, version %d/%d): %.95s",
                   verified_uri, uri_count, verified_version, version_count, error);
        return result;
    }

    if (db_exec_checked(db, "COMMIT", error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, uri_rows + version_rows,
                   "commit appinfo: %.150s", error);
        return result;
    }

    result_set(&result, STAGE_OK, 1, uri_rows + version_rows,
               "updated and verified %d URI + %d version row(s)", uri_rows, version_rows);
    return result;
}

static int update_appdb_json(sqlite3 *db, const char *title_id,
                             int *rows, char *error, size_t error_size)
{
    static const char sql[] =
        "UPDATE tbl_contentinfo "
        "SET AppInfoJson=json_set(AppInfoJson, "
        "'$.CONTENT_VERSION', ?1, '$.VERSION_FILE_URI', ?2) "
        "WHERE titleId=?3 AND json_valid(AppInfoJson)=1 AND json_type(AppInfoJson)='object'";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        return -1;
    }
    if (sqlite3_bind_text(stmt, 1, TARGET_VERSION, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, TARGET_URI, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, title_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        snprintf(error, error_size, "failed to bind app.db update: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        snprintf(error, error_size, "%s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    *rows = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    return 0;
}

static stage_result_t patch_appdb_title(sqlite3 *db, const char *title_id)
{
    stage_result_t result;
    char error[192] = {0};
    int total_rows = 0, object_rows = 0, updated_rows = 0, verified_rows = 0;

    result_set(&result, STAGE_MISSING, 0, 0, "no matching rows");
    if (db_exec_checked(db, "BEGIN IMMEDIATE", error, sizeof(error)) != 0) {
        result_set(&result, STAGE_ERROR, 0, 0, "begin transaction: %.150s", error);
        return result;
    }

    if (query_count(db, "SELECT COUNT(*) FROM tbl_contentinfo WHERE titleId=?1",
                    title_id, NULL, NULL, &total_rows, error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 0, 0, "check app.db rows: %.150s", error);
        return result;
    }
    if (total_rows == 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        return result;
    }

    if (query_count(db,
                    "SELECT COUNT(*) FROM tbl_contentinfo WHERE titleId=?1 AND json_valid(AppInfoJson)=1 AND json_type(AppInfoJson)='object'",
                    title_id, NULL, NULL, &object_rows, error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, 0, "JSON validation query failed: %.135s", error);
        return result;
    }
    if (object_rows != total_rows) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, 0,
                   "AppInfoJson must be a valid JSON object (%d of %d valid)",
                   object_rows, total_rows);
        return result;
    }

    if (update_appdb_json(db, title_id, &updated_rows, error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, 0, "update app.db: %.150s", error);
        return result;
    }
    if (updated_rows != total_rows ||
        query_count(db,
                    "SELECT COUNT(*) FROM tbl_contentinfo WHERE titleId=?1 AND json_extract(AppInfoJson,'$.CONTENT_VERSION')=?2 AND json_extract(AppInfoJson,'$.VERSION_FILE_URI')=?3",
                    title_id, TARGET_VERSION, TARGET_URI, &verified_rows,
                    error, sizeof(error)) != 0 || verified_rows != total_rows) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, updated_rows,
                   "app.db verification failed (%d of %d verified): %.115s",
                   verified_rows, total_rows, error);
        return result;
    }
    if (db_exec_checked(db, "COMMIT", error, sizeof(error)) != 0) {
        db_exec_checked(db, "ROLLBACK", error, sizeof(error));
        result_set(&result, STAGE_ERROR, 1, updated_rows, "commit app.db: %.150s", error);
        return result;
    }

    result_set(&result, STAGE_OK, 1, updated_rows,
               "updated and verified %d AppInfoJson row(s)", updated_rows);
    return result;
}

static int replace_json_string(cJSON *root, const char *key, const char *value)
{
    cJSON *replacement;
    if (cJSON_GetObjectItemCaseSensitive(root, key) == NULL) {
        return cJSON_AddStringToObject(root, key, value) ? 0 : -1;
    }
    replacement = cJSON_CreateString(value);
    if (replacement == NULL) {
        return -1;
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(root, key, replacement)) {
        cJSON_Delete(replacement);
        return -1;
    }
    return 0;
}

static int replace_json_key_variants(cJSON *root, const char *const *keys,
                                     size_t key_count, const char *fallback_key,
                                     const char *value)
{
    int found = 0;
    for (size_t i = 0; i < key_count; ++i) {
        if (cJSON_GetObjectItemCaseSensitive(root, keys[i]) != NULL) {
            if (replace_json_string(root, keys[i], value) != 0) {
                return -1;
            }
            found = 1;
        }
    }
    if (!found) {
        return replace_json_string(root, fallback_key, value);
    }
    return 0;
}

static int write_file_atomic(const char *path, const char *data, size_t length,
                             char *error, size_t error_size)
{
    struct stat original_stat;
    char temp_path[512];
    int fd = -1;
    size_t written = 0;

    if (stat(path, &original_stat) != 0) {
        snprintf(error, error_size, "stat original file failed: %s", strerror(errno));
        return -1;
    }
    if (snprintf(temp_path, sizeof(temp_path), "%s.ytblocker.tmp.%ld",
                 path, (long)getpid()) >= (int)sizeof(temp_path)) {
        snprintf(error, error_size, "temporary path is too long");
        return -1;
    }

    fd = open(temp_path, O_WRONLY | O_CREAT | O_EXCL, original_stat.st_mode & 07777);
    if (fd < 0) {
        snprintf(error, error_size, "create temporary file failed: %s", strerror(errno));
        return -1;
    }

    if (fchown(fd, original_stat.st_uid, original_stat.st_gid) != 0) {
        snprintf(error, error_size, "preserve file owner failed: %s", strerror(errno));
        close(fd);
        unlink(temp_path);
        return -1;
    }
    if (fchmod(fd, original_stat.st_mode & 07777) != 0) {
        snprintf(error, error_size, "preserve file permissions failed: %s", strerror(errno));
        close(fd);
        unlink(temp_path);
        return -1;
    }

    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            snprintf(error, error_size, "write temporary file failed: %s", strerror(errno));
            close(fd);
            unlink(temp_path);
            return -1;
        }
        if (count == 0) {
            snprintf(error, error_size, "write temporary file returned zero bytes");
            close(fd);
            unlink(temp_path);
            return -1;
        }
        written += (size_t)count;
    }

    if (fsync(fd) != 0) {
        snprintf(error, error_size, "fsync temporary file failed: %s", strerror(errno));
        close(fd);
        unlink(temp_path);
        return -1;
    }
    if (close(fd) != 0) {
        snprintf(error, error_size, "close temporary file failed: %s", strerror(errno));
        unlink(temp_path);
        return -1;
    }
    if (rename(temp_path, path) != 0) {
        snprintf(error, error_size, "replace original file failed: %s", strerror(errno));
        unlink(temp_path);
        return -1;
    }
    return 0;
}

static stage_result_t patch_param_json(const char *json_path)
{
    static const char *const content_version_keys[] = {
        "contentVersion", "CONTENT_VERSION", "contentversion"
    };
    static const char *const uri_keys[] = {
        "versionFileUri", "versionFileURI", "VERSION_FILE_URI"
    };
    stage_result_t result;
    struct stat st;
    FILE *fp = NULL;
    char *input = NULL;
    char *output = NULL;
    size_t length;
    size_t bytes_read;
    cJSON *root = NULL;
    char error[192] = {0};

    result_set(&result, STAGE_MISSING, 0, 0, "file not present");
    if (stat(json_path, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return result;
        result_set(&result, STAGE_ERROR, 0, 0, "stat failed: %.150s", strerror(errno));
        return result;
    }
    result.present = 1;

    if (st.st_size <= 0 || (unsigned long long)st.st_size > MAX_JSON_BYTES) {
        result_set(&result, STAGE_ERROR, 1, 0, "file size is invalid (%lld bytes)",
                   (long long)st.st_size);
        return result;
    }

    fp = fopen(json_path, "rb");
    if (fp == NULL) {
        result_set(&result, STAGE_ERROR, 1, 0, "open failed: %.150s", strerror(errno));
        return result;
    }
    length = (size_t)st.st_size;
    input = (char *)malloc(length + 1);
    if (input == NULL) {
        fclose(fp);
        result_set(&result, STAGE_ERROR, 1, 0, "out of memory reading JSON");
        return result;
    }
    bytes_read = fread(input, 1, length, fp);
    if (bytes_read != length || ferror(fp)) {
        snprintf(error, sizeof(error), "read failed or file changed while reading");
        fclose(fp);
        free(input);
        result_set(&result, STAGE_ERROR, 1, 0, "%s", error);
        return result;
    }
    if (fclose(fp) != 0) {
        free(input);
        result_set(&result, STAGE_ERROR, 1, 0, "close after read failed: %.140s", strerror(errno));
        return result;
    }
    input[length] = '\0';

    root = cJSON_Parse(input);
    free(input);
    input = NULL;
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) cJSON_Delete(root);
        result_set(&result, STAGE_ERROR, 1, 0, "JSON is invalid or root is not an object");
        return result;
    }

    if (replace_json_key_variants(root, content_version_keys,
                                  sizeof(content_version_keys) / sizeof(content_version_keys[0]),
                                  "contentVersion", TARGET_VERSION) != 0 ||
        replace_json_key_variants(root, uri_keys,
                                  sizeof(uri_keys) / sizeof(uri_keys[0]),
                                  "versionFileUri", TARGET_URI) != 0) {
        cJSON_Delete(root);
        result_set(&result, STAGE_ERROR, 1, 0, "unable to update JSON fields");
        return result;
    }

    output = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    root = NULL;
    if (output == NULL) {
        result_set(&result, STAGE_ERROR, 1, 0, "serialize JSON failed");
        return result;
    }

    if (write_file_atomic(json_path, output, strlen(output), error, sizeof(error)) != 0) {
        cJSON_free(output);
        result_set(&result, STAGE_ERROR, 1, 0, "atomic write failed: %.135s", error);
        return result;
    }
    cJSON_free(output);

    /* Re-open and parse the final file so success means the write was verifiable. */
    fp = fopen(json_path, "rb");
    if (fp == NULL) {
        result_set(&result, STAGE_ERROR, 1, 0, "re-open after write failed: %.130s", strerror(errno));
        return result;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        result_set(&result, STAGE_ERROR, 1, 0, "seek after write failed");
        return result;
    }
    long final_length = ftell(fp);
    if (final_length <= 0 || (unsigned long long)final_length > MAX_JSON_BYTES) {
        fclose(fp);
        result_set(&result, STAGE_ERROR, 1, 0, "invalid final JSON file size");
        return result;
    }
    rewind(fp);
    input = (char *)malloc((size_t)final_length + 1);
    if (input == NULL) {
        fclose(fp);
        result_set(&result, STAGE_ERROR, 1, 0, "out of memory verifying JSON");
        return result;
    }
    bytes_read = fread(input, 1, (size_t)final_length, fp);
    fclose(fp);
    input[bytes_read] = '\0';
    root = (bytes_read == (size_t)final_length) ? cJSON_Parse(input) : NULL;
    free(input);

    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) cJSON_Delete(root);
        result_set(&result, STAGE_ERROR, 1, 0, "final JSON could not be parsed");
        return result;
    }

    cJSON *version_item = NULL;
    for (size_t i = 0; i < sizeof(content_version_keys) / sizeof(content_version_keys[0]); ++i) {
        version_item = cJSON_GetObjectItemCaseSensitive(root, content_version_keys[i]);
        if (version_item != NULL) break;
    }
    cJSON *uri_item = NULL;
    for (size_t i = 0; i < sizeof(uri_keys) / sizeof(uri_keys[0]); ++i) {
        uri_item = cJSON_GetObjectItemCaseSensitive(root, uri_keys[i]);
        if (uri_item != NULL) break;
    }
    int verified = cJSON_IsString(version_item) && version_item->valuestring != NULL &&
                   strcmp(version_item->valuestring, TARGET_VERSION) == 0 &&
                   cJSON_IsString(uri_item) && uri_item->valuestring != NULL &&
                   strcmp(uri_item->valuestring, TARGET_URI) == 0;
    cJSON_Delete(root);
    if (!verified) {
        result_set(&result, STAGE_ERROR, 1, 0, "final JSON values did not match target values");
        return result;
    }

    result_set(&result, STAGE_OK, 1, 1, "updated and re-read successfully");
    return result;
}

static const char *status_word(const stage_result_t *result)
{
    if (result->status == STAGE_OK) return "OK";
    if (result->status == STAGE_ERROR) return "ERROR";
    return "MISSING";
}

static void append_text(char *buffer, size_t capacity, const char *fmt, ...)
{
    size_t used = strlen(buffer);
    va_list args;
    if (used >= capacity) return;
    va_start(args, fmt);
    vsnprintf(buffer + used, capacity - used, fmt, args);
    va_end(args);
}

static int any_present(const title_result_t *r)
{
    return r->appinfo.present || r->appdb.present ||
           r->system_param.present || r->user_param.present;
}

static int all_ok(const title_result_t *r)
{
    return r->appinfo.status == STAGE_OK && r->appdb.status == STAGE_OK &&
           r->system_param.status == STAGE_OK && r->user_param.status == STAGE_OK;
}

int main(void)
{
    char appinfo_path[512];
    char appdb_path[512];
    sqlite3 *appinfo_db = NULL;
    sqlite3 *app_db = NULL;
    char error[256] = {0};
    title_result_t results[TARGET_TITLE_COUNT];
    int appinfo_open_ok = 0;
    int appdb_open_ok = 0;
    int found_titles = 0;
    int complete_titles = 0;
    int incomplete_titles = 0;
    char report[2600] = {0};

    memset(results, 0, sizeof(results));
    for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
        result_set(&results[i].appinfo, STAGE_MISSING, 0, 0, "no matching records");
        result_set(&results[i].appdb, STAGE_MISSING, 0, 0, "no matching records");
        result_set(&results[i].system_param, STAGE_MISSING, 0, 0, "file not present");
        result_set(&results[i].user_param, STAGE_MISSING, 0, 0, "file not present");
    }

    /* Keep the payload silent until the final result notification. */

    make_target_path(appinfo_path, sizeof(appinfo_path), "/system_data/priv/mms/appinfo.db");
    make_target_path(appdb_path, sizeof(appdb_path), "/system_data/priv/mms/app.db");

    if (open_database(appinfo_path, &appinfo_db, error, sizeof(error)) == 0) {
        appinfo_open_ok = 1;
        for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
            results[i].appinfo = patch_appinfo_title(appinfo_db, TARGET_TITLE_IDS[i]);
        }
    } else {
        for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
            result_set(&results[i].appinfo, STAGE_ERROR, 0, 0, "%.180s", error);
        }
    }

    memset(error, 0, sizeof(error));
    if (open_database(appdb_path, &app_db, error, sizeof(error)) == 0) {
        appdb_open_ok = 1;
        for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
            results[i].appdb = patch_appdb_title(app_db, TARGET_TITLE_IDS[i]);
        }
    } else {
        for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
            result_set(&results[i].appdb, STAGE_ERROR, 0, 0, "%.180s", error);
        }
    }

    for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
        char system_param_path[512];
        char user_param_path[512];
        char absolute_path[256];
        snprintf(absolute_path, sizeof(absolute_path),
                 "/system_data/priv/appmeta/%s/param.json", TARGET_TITLE_IDS[i]);
        make_target_path(system_param_path, sizeof(system_param_path), absolute_path);
        snprintf(absolute_path, sizeof(absolute_path),
                 "/user/appmeta/%s/param.json", TARGET_TITLE_IDS[i]);
        make_target_path(user_param_path, sizeof(user_param_path), absolute_path);
        results[i].system_param = patch_param_json(system_param_path);
        results[i].user_param = patch_param_json(user_param_path);
    }

    if (appinfo_db != NULL) sqlite3_close(appinfo_db);
    if (app_db != NULL) sqlite3_close(app_db);
    append_text(report, sizeof(report), "YT-Blocker result\n");
    append_text(report, sizeof(report), "appinfo.db: %s | app.db: %s\n",
                appinfo_open_ok ? "opened" : "OPEN FAILED",
                appdb_open_ok ? "opened" : "OPEN FAILED");

    for (size_t i = 0; i < TARGET_TITLE_COUNT; ++i) {
        title_result_t *r = &results[i];
        if (!any_present(r)) continue;

        ++found_titles;
        if (all_ok(r)) {
            ++complete_titles;
        } else {
            ++incomplete_titles;
        }

        append_text(report, sizeof(report), "%s: %s\n",
                    TARGET_TITLE_IDS[i], all_ok(r) ? "FULLY VERIFIED" : "INCOMPLETE");
        append_text(report, sizeof(report), " appinfo [%s]: %.145s\n",
                    status_word(&r->appinfo), r->appinfo.detail);
        append_text(report, sizeof(report), " app.db [%s]: %.145s\n",
                    status_word(&r->appdb), r->appdb.detail);
        append_text(report, sizeof(report), " system param [%s]: %.145s\n",
                    status_word(&r->system_param), r->system_param.detail);
        append_text(report, sizeof(report), " user param [%s]: %.145s\n",
                    status_word(&r->user_param), r->user_param.detail);
    }

    if (found_titles == 0) {
        append_text(report, sizeof(report),
                    "No target title ID was found in the available metadata.\n"
                    "No update-blocking success can be claimed.\n");
    } else if (incomplete_titles == 0) {
        append_text(report, sizeof(report),
                    "All detected title IDs passed file/database verification.\n"
                    "This does not replace the recommended DNS configuration.\n");
    } else {
        append_text(report, sizeof(report),
                    "WARNING: some locations failed or were not found.\n"
                    "Do not assume update blocking is active.\n");
    }
    append_text(report, sizeof(report), "Coded by EPINOR");
    notify_message("Done👍\nCoded By EPINOR");

    if (found_titles == 0) return 2;
    if (incomplete_titles != 0) return 1;
    return 0;
}
