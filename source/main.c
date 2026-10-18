#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "sqlite3.h"
#include "cJSON.h"

#define TARGET_VERSION "99.999.999"
#define TARGET_URI     "http://127.0.0.2"

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static void notify(const char *fmt, ...) {
    notify_request_t req;
    va_list args;

    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static int exec_sql(const char *db_path, const char *sql) {
    sqlite3 *db = NULL;
    char *err_msg = NULL;

    if (access(db_path, F_OK) != 0) {
        return -1;
    }

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    sqlite3_busy_timeout(db, 5000);

    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
    }

    sqlite3_close(db);
    return (rc == SQLITE_OK) ? 0 : -1;
}

static int patch_param_json(const char *json_path) {
    if (access(json_path, F_OK) != 0) {
        return -1;
    }

    FILE *fp = fopen(json_path, "rb");
    if (!fp) return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long len = ftell(fp);
    if (len <= 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[read_bytes] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    if (cJSON_GetObjectItemCaseSensitive(root, "contentVersion")) {
        cJSON_ReplaceItemInObjectCaseSensitive(root, "contentVersion", cJSON_CreateString(TARGET_VERSION));
    } else {
        cJSON_AddStringToObject(root, "contentVersion", TARGET_VERSION);
    }

    if (cJSON_GetObjectItemCaseSensitive(root, "versionFileUri")) {
        cJSON_ReplaceItemInObjectCaseSensitive(root, "versionFileUri", cJSON_CreateString(TARGET_URI));
    } else {
        cJSON_AddStringToObject(root, "versionFileUri", TARGET_URI);
    }

    char *out_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!out_str) return -1;

    fp = fopen(json_path, "wb");
    if (!fp) {
        free(out_str);
        return -1;
    }

    fwrite(out_str, 1, strlen(out_str), fp);
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
    free(out_str);

    return 0;
}

int main(void) {
    const char *title_ids[] = {"PPSA01650", "PPSA01651"};
    const size_t num_titles = sizeof(title_ids) / sizeof(title_ids[0]);

    char sql[512];
    char path[256];

    for (size_t i = 0; i < num_titles; i++) {
        const char *tid = title_ids[i];

        snprintf(sql, sizeof(sql),
                 "UPDATE tbl_appinfo SET val = '%s' WHERE titleId = '%s' AND key = 'VERSION_FILE_URI';"
                 "UPDATE tbl_appinfo SET val = '%s' WHERE titleId = '%s' AND key = 'CONTENT_VERSION';",
                 TARGET_URI, tid, TARGET_VERSION, tid);
        exec_sql("/system_data/priv/mms/appinfo.db", sql);

        snprintf(sql, sizeof(sql),
                 "UPDATE tbl_contentinfo SET AppInfoJson = json_set(AppInfoJson, "
                 "'$.CONTENT_VERSION', '%s', '$.VERSION_FILE_URI', '%s') "
                 "WHERE titleId = '%s';",
                 TARGET_VERSION, TARGET_URI, tid);
        exec_sql("/system_data/priv/mms/app.db", sql);

        snprintf(path, sizeof(path), "/system_data/priv/appmeta/%s/param.json", tid);
        patch_param_json(path);

        snprintf(path, sizeof(path), "/user/appmeta/%s/param.json", tid);
        patch_param_json(path);
    }

    sync();
    notify("Done👍\nCoded by EPINOR");

    return 0;
}