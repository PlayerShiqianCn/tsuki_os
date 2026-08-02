#include "tlx_kernel.h"
#include "tlx.h"
#include "process.h"
#include "fs.h"
#include "heap.h"
#include "utils.h"
#include "ps2.h"
#include "klog.h"
#include "console.h"

extern unsigned int timer_get_ticks(void);

#define TLX_CONTEXT_COUNT 16
#define TLX_MAX_FILE_BYTES 0x44000u

typedef struct {
    int used;
    Process* owner;
    TlxHandle handles[TLX_MAX_FDS];
    char cwd[TLX_MAX_PATH];
} TlxContext;

static TlxContext contexts[TLX_CONTEXT_COUNT];

static void tlx_copy_string(char* dst, const char* src, unsigned int capacity) {
    unsigned int i = 0;
    if (!dst || capacity == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static TlxContext* tlx_find_context(Process* owner, int create) {
    TlxContext* free_context = 0;

    if (!owner) return 0;
    for (int i = 0; i < TLX_CONTEXT_COUNT; i++) {
        if (contexts[i].used && contexts[i].owner == owner) {
            return &contexts[i];
        }
        if (!contexts[i].used && !free_context) free_context = &contexts[i];
    }

    if (!create || !free_context) return 0;
    tlx_process_init(owner);
    return tlx_find_context(owner, 0);
}

static void tlx_init_standard_handles(TlxContext* context) {
    if (!context) return;
    memset(context->handles, 0, sizeof(context->handles));
    context->handles[TLX_STD_INPUT].used = 1;
    context->handles[TLX_STD_INPUT].kind = TLX_HANDLE_KIND_INPUT;
    context->handles[TLX_STD_OUTPUT].used = 1;
    context->handles[TLX_STD_OUTPUT].kind = TLX_HANDLE_KIND_OUTPUT;
    context->handles[TLX_STD_ERROR].used = 1;
    context->handles[TLX_STD_ERROR].kind = TLX_HANDLE_KIND_ERROR;
}

void tlx_process_init(struct Process* process) {
    TlxContext* context = 0;
    TlxContext* free_context = 0;

    if (!process) return;
    for (int i = 0; i < TLX_CONTEXT_COUNT; i++) {
        if (contexts[i].used && contexts[i].owner == process) {
            context = &contexts[i];
            break;
        }
        if (!contexts[i].used && !free_context) free_context = &contexts[i];
    }
    if (!context) context = free_context;
    if (!context) return;

    memset(context, 0, sizeof(*context));
    context->used = 1;
    context->owner = process;
    context->cwd[0] = '/';
    context->cwd[1] = '\0';
    tlx_init_standard_handles(context);
}

void tlx_process_release(struct Process* process) {
    if (!process) return;
    for (int i = 0; i < TLX_CONTEXT_COUNT; i++) {
        if (contexts[i].used && contexts[i].owner == process) {
            memset(&contexts[i], 0, sizeof(contexts[i]));
            return;
        }
    }
}

static TlxContext* tlx_current_context(void) {
    return tlx_find_context(current_process, 1);
}

static TlxHandle* tlx_get_handle(TlxContext* context, int handle) {
    if (!context || handle < 0 || handle >= TLX_MAX_FDS) return 0;
    if (!context->handles[handle].used) return 0;
    return &context->handles[handle];
}

static int tlx_make_path(TlxContext* context, const char* input, char* output) {
    const char* source;
    int absolute;
    int pos = 0;

    if (!context || !input || !output) return 0;
    absolute = input[0] == '/';
    source = input;
    while (*source == '/') source++;

    if (*source == '\0') {
        output[0] = '/';
        output[1] = '\0';
        return 1;
    }

    if (!absolute && context->cwd[0] != '/' && context->cwd[1] != '\0') {
        for (int i = 0; context->cwd[i]; i++) {
            if (pos + 1 >= TLX_MAX_PATH) return 0;
            output[pos++] = context->cwd[i];
        }
        if (pos + 1 >= TLX_MAX_PATH) return 0;
        output[pos++] = '/';
    }

    for (int i = 0; source[i]; i++) {
        if (pos + 1 >= TLX_MAX_PATH) return 0;
        output[pos++] = source[i];
    }
    output[pos] = '\0';
    return 1;
}

static int tlx_open_existing(const char* path, SystemFile* file, char* actual_path) {
    static const char hidden_suffix[] = "._hid_";
    int length;

    if (!path || !file || !actual_path) return 0;
    if (sys_file_open(path, file)) {
        tlx_copy_string(actual_path, path, TLX_MAX_PATH);
        return 1;
    }

    length = strlen(path);
    if (length + (int)(sizeof(hidden_suffix) - 1) >= TLX_MAX_PATH) return 0;
    memcpy(actual_path, path, length);
    memcpy(actual_path + length, hidden_suffix, sizeof(hidden_suffix));
    if (!sys_file_open(actual_path, file)) return 0;
    return 1;
}

static int tlx_allocate_handle(TlxContext* context) {
    if (!context) return -1;
    for (int i = 3; i < TLX_MAX_FDS; i++) {
        if (!context->handles[i].used) return i;
    }
    return -1;
}

static int tlx_open_handle(TlxContext* context, const char* input_path, unsigned int flags) {
    char path[TLX_MAX_PATH];
    char actual_path[TLX_MAX_PATH];
    SystemFile file;
    int handle;

    if (!context || !input_path) return -TLX_EINVAL;
    if (flags & TLX_OPEN_APPEND) flags |= TLX_OPEN_WRITE;
    if (flags & TLX_OPEN_TRUNC) flags |= TLX_OPEN_WRITE;
    if (!(flags & (TLX_OPEN_READ | TLX_OPEN_WRITE))) flags |= TLX_OPEN_READ;
    if (flags & TLX_OPEN_CREATE) {
        int create_result;
        if (!tlx_make_path(context, input_path, path)) return -TLX_EOVERFLOW;
        create_result = fs_create_file(path);
        if (create_result == 0) return -TLX_ENOSPC;
        if (create_result < 0) {
            /* Already exists - that's OK if not EXCL */
            if (flags & TLX_OPEN_TRUNC) {
                /* Will be truncated below */
            }
        }
    } else {
        if (!tlx_make_path(context, input_path, path)) return -TLX_EOVERFLOW;
    }

    handle = tlx_allocate_handle(context);
    if (handle < 0) return -TLX_EMFILE;

    memset(&context->handles[handle], 0, sizeof(context->handles[handle]));
    context->handles[handle].used = 1;
    context->handles[handle].flags = flags;

    if (flags & TLX_OPEN_DIR) {
        if (strcmp(path, "/") == 0) {
            context->handles[handle].kind = TLX_HANDLE_KIND_DIR;
            tlx_copy_string(context->handles[handle].path, "/", TLX_MAX_PATH);
            return handle;
        }
        if (!sys_file_open(path, &file)) {
            context->handles[handle].used = 0;
            return -TLX_ENOENT;
        }
        if (file.type != 1) {
            context->handles[handle].used = 0;
            return -TLX_ENOT_DIR;
        }
        context->handles[handle].kind = TLX_HANDLE_KIND_DIR;
        context->handles[handle].size = file.size;
        context->handles[handle].inode = file.inode_num;
        tlx_copy_string(context->handles[handle].path, path, TLX_MAX_PATH);
        return handle;
    }

    if (!tlx_open_existing(path, &file, actual_path)) {
        context->handles[handle].used = 0;
        return -TLX_ENOENT;
    }
    if (file.type != 0) {
        context->handles[handle].used = 0;
        return -TLX_EIS_DIR;
    }
    if ((flags & TLX_OPEN_TRUNC) && (flags & TLX_OPEN_WRITE)) {
        fs_write_file(actual_path, 0, 0);
        file.size = 0;
    }

    context->handles[handle].kind = TLX_HANDLE_KIND_FILE;
    context->handles[handle].size = file.size;
    context->handles[handle].inode = file.inode_num;
    context->handles[handle].position = (flags & TLX_OPEN_APPEND) ? file.size : 0;
    tlx_copy_string(context->handles[handle].path, actual_path, TLX_MAX_PATH);
    return handle;
}

static int tlx_read_handle(TlxHandle* handle, void* buffer, unsigned int size) {
    AppFile file;
    int result;

    if (!handle) return -TLX_EINVAL;
    if (size == 0) return 0;
    if (!buffer) return -TLX_EINVAL;
    if (handle->kind != TLX_HANDLE_KIND_FILE) return -TLX_EBAD_HANDLE;
    if (!(handle->flags & TLX_OPEN_READ)) return -TLX_EPERM;
    if (!app_file_open(handle->path, &file)) return -TLX_EIO;
    file.current_pos = handle->position;
    result = app_file_read(&file, buffer, size);
    if (result > 0) {
        handle->position = file.current_pos;
        handle->size = file.sys_file.size;
    }
    return result;
}

static int tlx_write_file(TlxHandle* handle, const void* buffer, unsigned int size) {
    unsigned int new_size;
    unsigned char* snapshot;
    int result;

    if (!handle || !buffer) return -TLX_EINVAL;
    if (handle->kind != TLX_HANDLE_KIND_FILE) return -TLX_EBAD_HANDLE;
    if (!(handle->flags & TLX_OPEN_WRITE)) return -TLX_EPERM;
    if (handle->position > handle->size) return -TLX_EINVAL;
    if (size == 0) return 0;
    if (size > TLX_MAX_FILE_BYTES - handle->position) return -TLX_EOVERFLOW;

    new_size = handle->size;
    if (handle->position + size > new_size) new_size = handle->position + size;
    if (new_size > TLX_MAX_FILE_BYTES) return -TLX_ENOSPC;

    snapshot = (unsigned char*)malloc(new_size ? new_size : 1);
    if (!snapshot) return -TLX_ENOSPC;
    memset(snapshot, 0, new_size);
    if (handle->size > 0) {
        result = fs_read_file(handle->path, snapshot, handle->size);
        if (result != (int)handle->size) {
            free(snapshot);
            return -TLX_EIO;
        }
    }

    memcpy(snapshot + handle->position, buffer, size);
    result = fs_write_file(handle->path, snapshot, new_size);
    free(snapshot);
    if (result != (int)new_size) return -TLX_ENOSPC;

    handle->size = new_size;
    handle->position += size;
    return (int)size;
}

static int tlx_write_console(const void* buffer, unsigned int size) {
    char chunk[65];
    unsigned int written = 0;

    if (!buffer && size) return -TLX_EINVAL;
    while (written < size) {
        unsigned int count = size - written;
        if (count > sizeof(chunk) - 1) count = sizeof(chunk) - 1;
        memcpy(chunk, (const unsigned char*)buffer + written, count);
        chunk[count] = '\0';
        klog_write(chunk);
        written += count;
    }
    return (int)size;
}

static int tlx_read_console(void* buffer, unsigned int size) {
    unsigned int count = 0;
    char key;

    if (!buffer && size) return -TLX_EINVAL;
    while (count < size && ps2_has_key()) {
        key = ps2_getchar();
        if (!key) break;
        ((char*)buffer)[count++] = key;
    }
    return (int)count;
}

static int tlx_seek_handle(TlxHandle* handle, int offset, int whence) {
    int base;
    int target;

    if (!handle || handle->kind != TLX_HANDLE_KIND_FILE) return -TLX_EBAD_HANDLE;
    if (whence == TLX_SEEK_START) base = 0;
    else if (whence == TLX_SEEK_HERE) base = (int)handle->position;
    else if (whence == TLX_SEEK_END) base = (int)handle->size;
    else return -TLX_EINVAL;

    target = base + offset;
    if (target < 0 || (unsigned int)target > TLX_MAX_FILE_BYTES) return -TLX_EINVAL;
    handle->position = (unsigned int)target;
    return target;
}

static int tlx_stat_handle(TlxHandle* handle, TlxFileInfo* info) {
    if (!handle || !info) return -TLX_EINVAL;
    info->size = handle->size;
    info->position = handle->position;
    info->kind = handle->kind;
    info->flags = handle->flags;
    return 0;
}

static int tlx_list_path(TlxContext* context, const char* input, char* buffer, unsigned int capacity) {
    char path[TLX_MAX_PATH];
    const char* lookup;
    int result;

    if (!context || !buffer || capacity == 0) return -TLX_EINVAL;
    if (!input || input[0] == '\0') input = context->cwd;
    if (!tlx_make_path(context, input, path)) return -TLX_EOVERFLOW;
    lookup = strcmp(path, "/") == 0 ? "" : path;
    result = fs_get_file_list(buffer, (int)capacity, lookup);
    if (result == 0 && strcmp(path, "/") != 0) return -TLX_ENOENT;
    return result;
}

static int tlx_is_restricted(unsigned int op, unsigned int arg1) {
    if (!current_process) return 0;
    if (current_process->sandbox_level == SANDBOX_NONE) return 0;
    if (current_process->sandbox_level == SANDBOX_BASIC) {
        if (op == TLX_OP_WRITE || op == TLX_OP_SPAWN) return 1;
        if (op == TLX_OP_OPEN && (arg1 & (TLX_OPEN_WRITE | TLX_OPEN_CREATE | TLX_OPEN_TRUNC))) return 1;
        return 0;
    }
    return !(op == TLX_OP_CLOSE || op == TLX_OP_READ || op == TLX_OP_FSTAT ||
             op == TLX_OP_GETPID || op == TLX_OP_GETPPID || op == TLX_OP_SLEEP ||
             op == TLX_OP_YIELD || op == TLX_OP_CLOCK || op == TLX_OP_IDENTITY ||
             op == TLX_OP_UNLINK || op == TLX_OP_MKDIR);
}

static int tlx_wait_for_wakeup(void) {
    while (current_process && current_process->state == PROCESS_BLOCKED) {
        __asm__ volatile("sti; hlt; cli");
    }
    return 0;
}

int tlx_dispatch(Registers* regs) {
    TlxContext* context;
    TlxHandle* handle;
    TlxIdentity identity;
    char path[TLX_MAX_PATH];
    int op;
    int result;

    if (!regs) return -TLX_EINVAL;
    context = tlx_current_context();
    if (!context) return -TLX_EBUSY;
    op = (int)regs->ebx;
    if (tlx_is_restricted((unsigned int)op, op == TLX_OP_OPEN ? regs->edx : regs->ecx)) return -TLX_EPERM;

    switch (op) {
        case TLX_OP_OPEN:
            return tlx_open_handle(context, (const char*)regs->ecx, regs->edx);
        case TLX_OP_CLOSE:
            handle = tlx_get_handle(context, (int)regs->ecx);
            if ((int)regs->ecx >= 0 && (int)regs->ecx < 3) return 0;
            if (!handle) return -TLX_EBAD_HANDLE;
            handle->used = 0;
            return 0;
        case TLX_OP_READ:
            handle = tlx_get_handle(context, (int)regs->ecx);
            if (!handle) return -TLX_EBAD_HANDLE;
            if (handle->kind == TLX_HANDLE_KIND_INPUT) return tlx_read_console((void*)regs->edx, regs->esi);
            return tlx_read_handle(handle, (void*)regs->edx, regs->esi);
        case TLX_OP_WRITE:
            handle = tlx_get_handle(context, (int)regs->ecx);
            if (!handle) return -TLX_EBAD_HANDLE;
            if (handle->kind == TLX_HANDLE_KIND_OUTPUT || handle->kind == TLX_HANDLE_KIND_ERROR) {
                return tlx_write_console((const void*)regs->edx, regs->esi);
            }
            return tlx_write_file(handle, (const void*)regs->edx, regs->esi);
        case TLX_OP_SEEK:
            handle = tlx_get_handle(context, (int)regs->ecx);
            return tlx_seek_handle(handle, (int)regs->edx, (int)regs->esi);
        case TLX_OP_FSTAT:
            handle = tlx_get_handle(context, (int)regs->ecx);
            return tlx_stat_handle(handle, (TlxFileInfo*)regs->edx);
        case TLX_OP_LIST:
            return tlx_list_path(context, (const char*)regs->ecx, (char*)regs->edx, regs->esi);
        case TLX_OP_GETPID:
            return current_process ? current_process->pid : -1;
        case TLX_OP_GETPPID:
            return current_process ? current_process->parent_pid : -1;
        case TLX_OP_SLEEP:
            process_sleep(regs->ecx);
            return tlx_wait_for_wakeup();
        case TLX_OP_YIELD:
            process_sleep(1);
            return tlx_wait_for_wakeup();
        case TLX_OP_CLOCK:
            return (int)timer_get_ticks();
        case TLX_OP_GETCWD:
            if (!regs->ecx || regs->edx == 0) return -TLX_EINVAL;
            result = strlen(context->cwd);
            if ((unsigned int)result + 1 > regs->edx) return -TLX_EOVERFLOW;
            memcpy((void*)regs->ecx, context->cwd, result + 1);
            return result;
        case TLX_OP_CHDIR:
            if (!tlx_make_path(context, (const char*)regs->ecx, path)) return -TLX_EOVERFLOW;
            if (strcmp(path, "/") == 0) {
                tlx_copy_string(context->cwd, "/", TLX_MAX_PATH);
                return 0;
            }
            {
                SystemFile directory;
                if (!sys_file_open(path, &directory)) return -TLX_ENOENT;
                if (directory.type != 1) return -TLX_ENOT_DIR;
            }
            tlx_copy_string(context->cwd, path, TLX_MAX_PATH);
            return 0;
        case TLX_OP_SPAWN:
            return console_launch_tsk((const char*)regs->ecx) ? 0 : -TLX_ENOENT;
        case TLX_OP_IDENTITY:
            if (!regs->ecx) return -TLX_EINVAL;
            memset(&identity, 0, sizeof(identity));
            tlx_copy_string(identity.name, "Tsuki OS", sizeof(identity.name));
            tlx_copy_string(identity.release, "tlx-1", sizeof(identity.release));
            identity.abi_version = TLX_ABI_VERSION;
            identity.feature_bits = 0x000001FFu;
            memcpy((void*)regs->ecx, &identity, sizeof(identity));
            return 0;
        case TLX_OP_UNLINK:
            if (!regs->ecx) return -TLX_EINVAL;
            if (!tlx_make_path(context, (const char*)regs->ecx, path)) return -TLX_EOVERFLOW;
            result = fs_delete_file(path);
            if (result == 0) return -TLX_ENOENT;
            if (result < 0) return -TLX_EIS_DIR;
            return 0;
        case TLX_OP_MKDIR:
            if (!regs->ecx) return -TLX_EINVAL;
            if (!tlx_make_path(context, (const char*)regs->ecx, path)) return -TLX_EOVERFLOW;
            result = fs_mkdir(path);
            if (result == 0) return -TLX_ENOSPC;
            if (result < 0) return -TLX_EEXIST;
            return 0;
        default:
            return -TLX_ENOSYS;
    }
}

