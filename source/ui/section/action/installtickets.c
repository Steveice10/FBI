#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "action.h"
#include "../task/task.h"
#include "../../error.h"
#include "../../info.h"
#include "../../list.h"
#include "../../prompt.h"
#include "../../ui.h"
#include "../../../core/linkedlist.h"
#include "../../../core/screen.h"
#include "../../../core/util.h"

typedef struct {
    linked_list* items;
    list_item* selected;
    file_info* target;

    list_item* curr;

    bool all;
    bool delete;

    u32 numDeleted;

    data_op_info installInfo;
    Handle cancelEvent;
} install_tickets_data;

static Result action_install_tickets_is_src_directory(void* data, u32 index, bool* isDirectory) {
    *isDirectory = false;
    return 0;
}

static Result action_install_tickets_make_dst_directory(void* data, u32 index) {
    return 0;
}

static Result action_install_tickets_open_src(void* data, u32 index, u32* handle) {
    install_tickets_data* installData = (install_tickets_data*) data;

    if(installData->all) {
        linked_list_iter iter;
        linked_list_iterate(installData->items, &iter);

        u32 count = 0;
        while(linked_list_iter_has_next(&iter) && count < index + 1 - installData->numDeleted) {
            list_item* item = linked_list_iter_next(&iter);
            file_info* info = (file_info*) item->data;

            size_t len = strlen(info->path);
            if(len > 4 && strcmp(&info->path[len - 4], ".tik") == 0) {
                installData->curr = item;
                count++;
            }
        }
    } else {
        installData->curr = installData->selected;
    }

    file_info* info = (file_info*) installData->curr->data;

    Result res = 0;

    FS_Path* fsPath = util_make_path_utf8(info->path);
    if(fsPath != NULL) {
        res = FSUSER_OpenFile(handle, *info->archive, *fsPath, FS_OPEN_READ, 0);

        util_free_path_utf8(fsPath);
    } else {
        res = R_FBI_OUT_OF_MEMORY;
    }

    return res;
}

static Result action_install_tickets_close_src(void* data, u32 index, bool succeeded, u32 handle) {
    install_tickets_data* installData = (install_tickets_data*) data;

    file_info* info = (file_info*) installData->curr->data;

    Result res = 0;

    if(R_SUCCEEDED(res = FSFILE_Close(handle)) && installData->delete && succeeded) {
        FS_Path* fsPath = util_make_path_utf8(info->path);
        if(fsPath != NULL) {
            if(R_SUCCEEDED(FSUSER_DeleteFile(*info->archive, *fsPath))) {
                linked_list_remove(installData->items, installData->curr);
                task_free_file(installData->curr);

                installData->curr = NULL;

                installData->numDeleted++;
            }

            util_free_path_utf8(fsPath);
        } else {
            res = R_FBI_OUT_OF_MEMORY;
        }
    }

    return res;
}

static Result action_install_tickets_get_src_size(void* data, u32 handle, u64* size) {
    return FSFILE_GetSize(handle, size);
}

static Result action_install_tickets_read_src(void* data, u32 handle, u32* bytesRead, void* buffer, u64 offset, u32 size) {
    return FSFILE_Read(handle, bytesRead, offset, buffer, size);
}

static Result action_install_tickets_open_dst(void* data, u32 index, void* initialReadBlock, u32* handle) {
    return AM_InstallTicketBegin(handle);
}

static Result action_install_tickets_close_dst(void* data, u32 index, bool succeeded, u32 handle) {
    if(succeeded) {
        return AM_InstallTicketFinish(handle);
    } else {
        return AM_InstallTicketAbort(handle);
    }
}

static Result action_install_tickets_write_dst(void* data, u32 handle, u32* bytesWritten, void* buffer, u64 offset, u32 size) {
    return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}

static bool action_install_tickets_error(void* data, u32 index, Result res) {
    install_tickets_data* installData = (install_tickets_data*) data;

    file_info* info = (file_info*) installData->curr->data;

    if(res == R_FBI_CANCELLED) {
        prompt_display("Failure", "Install cancelled.", COLOR_TEXT, false, info, NULL, ui_draw_file_info, NULL);
        return false;
    } else {
        volatile bool dismissed = false;
        error_display_res(&dismissed, info, ui_draw_file_info, res, "Failed to install ticket.");

        while(!dismissed) {
            svcSleepThread(1000000);
        }
    }

    return index < installData->installInfo.total - 1;
}

static void action_install_tickets_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    install_tickets_data* installData = (install_tickets_data*) data;

    if(installData->curr != NULL) {
        ui_draw_file_info(view, installData->curr->data, x1, y1, x2, y2);
    } else if(installData->target != NULL) {
        ui_draw_file_info(view, installData->target, x1, y1, x2, y2);
    }
}

static void action_install_tickets_update(ui_view* view, void* data, float* progress, char* text) {
    install_tickets_data* installData = (install_tickets_data*) data;

    if(installData->installInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(!installData->installInfo.premature) {
            prompt_display("Success", "Install finished.", COLOR_TEXT, false, NULL, NULL, NULL, NULL);
        }

        free(installData);

        return;
    }

    if(hidKeysDown() & KEY_B) {
        svcSignalEvent(installData->cancelEvent);
    }

    *progress = installData->installInfo.currTotal != 0 ? (float) ((double) installData->installInfo.currProcessed / (double) installData->installInfo.currTotal) : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu\n%.2f MB / %.2f MB", installData->installInfo.processed, installData->installInfo.total, installData->installInfo.currProcessed / 1024.0 / 1024.0, installData->installInfo.currTotal / 1024.0 / 1024.0);
}

static void action_install_tickets_onresponse(ui_view* view, void* data, bool response) {
    install_tickets_data* installData = (install_tickets_data*) data;

    if(response) {
        installData->cancelEvent = task_data_op(&installData->installInfo);
        if(installData->cancelEvent != 0) {
            info_display("Installing ticket(s)", "Press B to cancel.", true, data, action_install_tickets_update, action_install_tickets_draw_top);
        } else {
            error_display(NULL, NULL, NULL, "Failed to initiate ticket installation.");

            free(installData);
        }
    } else {
        free(installData);
    }
}

static void action_install_tickets_internal(linked_list* items, list_item* selected, file_info* target, const char* message, bool all, bool delete) {
    install_tickets_data* data = (install_tickets_data*) calloc(1, sizeof(install_tickets_data));
    if(data == NULL) {
        error_display(NULL, NULL, NULL, "Failed to allocate install tickets data.");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->target = target;

    data->all = all;
    data->delete = delete;

    data->numDeleted = 0;

    data->installInfo.data = data;

    data->installInfo.op = DATAOP_COPY;

    data->installInfo.copyEmpty = false;

    data->installInfo.isSrcDirectory = action_install_tickets_is_src_directory;
    data->installInfo.makeDstDirectory = action_install_tickets_make_dst_directory;

    data->installInfo.openSrc = action_install_tickets_open_src;
    data->installInfo.closeSrc = action_install_tickets_close_src;
    data->installInfo.getSrcSize = action_install_tickets_get_src_size;
    data->installInfo.readSrc = action_install_tickets_read_src;

    data->installInfo.openDst = action_install_tickets_open_dst;
    data->installInfo.closeDst = action_install_tickets_close_dst;
    data->installInfo.writeDst = action_install_tickets_write_dst;

    data->installInfo.error = action_install_tickets_error;

    data->cancelEvent = 0;

    if(all) {
        linked_list_iter iter;
        linked_list_iterate(data->items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            list_item* item = linked_list_iter_next(&iter);
            file_info* info = (file_info*) item->data;

            size_t len = strlen(info->path);
            if(len > 4 && strcmp(&info->path[len - 4], ".tik") == 0) {
                data->installInfo.total++;
            }
        }
    } else {
        data->installInfo.total = 1;
    }

    prompt_display("Confirmation", message, COLOR_TEXT, true, data, NULL, action_install_tickets_draw_top, action_install_tickets_onresponse);
}

void action_install_ticket(linked_list* items, list_item* selected, file_info* target) {
    action_install_tickets_internal(items, selected, target, "Install the selected ticket?", false, false);
}

void action_install_ticket_delete(linked_list* items, list_item* selected, file_info* target) {
    action_install_tickets_internal(items, selected, target, "Install and delete the selected ticket?", false, true);
}

void action_install_tickets(linked_list* items, list_item* selected, file_info* target) {
    action_install_tickets_internal(items, selected, target, "Install all tickets in the current directory?", true, false);
}

void action_install_tickets_delete(linked_list* items, list_item* selected, file_info* target) {
    action_install_tickets_internal(items, selected, target, "Install and delete all tickets in the current directory?", true, true);
}