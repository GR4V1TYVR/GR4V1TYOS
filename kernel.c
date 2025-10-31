/*
  GR4V1TYOS v4.0 - Full Virtual Shell with App Library and App Install
  - Virtual filesystem in memory, autosaves to savdisk.txt
  - Built-in apps: calculator, notepad (saves to vfs), numbergame, about
  - App install/uninstall and installed apps stored in /apps/*.savapp
  - Commands: help, ls, cd, back, mkdir, rmdir, write, cat, rm, clear, wipe, apps, run, install, uninstall, appinfo, exit
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_NAME 64
#define MAX_CONTENT 4096
#define MAX_FILES 256
#define MAX_DIRS 128
#define DISK_FILE "savdisk.txt"

typedef struct File {
    char name[MAX_NAME];
    char *content; // allocated
} File;

typedef struct Directory {
    char name[MAX_NAME];
    struct Directory* parent;
    struct Directory* subdirs[MAX_DIRS];
    File* files[MAX_FILES];
    int dir_count;
    int file_count;
} Directory;

typedef struct App {
    char name[64];
    char desc[256];
    char code[MAX_CONTENT]; // for installed apps, stored text; for builtins, small tag
    int builtin; // 1 = builtin, 0 = installed
} App;

// Globals
Directory* root;
Directory* current_dir;
App apps[256];
int app_count = 0;

// ---------- Utilities ----------
Directory* create_dir(const char* name, Directory* parent) {
    Directory* d = (Directory*)malloc(sizeof(Directory));
    strncpy(d->name, name, MAX_NAME-1);
    d->name[MAX_NAME-1] = '\0';
    d->parent = parent;
    d->dir_count = 0;
    d->file_count = 0;
    for (int i=0;i<MAX_DIRS;i++) d->subdirs[i] = NULL;
    for (int i=0;i<MAX_FILES;i++) d->files[i] = NULL;
    return d;
}

File* create_file(const char* name, const char* content) {
    File* f = (File*)malloc(sizeof(File));
    strncpy(f->name, name, MAX_NAME-1);
    f->name[MAX_NAME-1] = '\0';
    if (content) {
        f->content = (char*)malloc(strlen(content)+1);
        strcpy(f->content, content);
    } else {
        f->content = (char*)malloc(1);
        f->content[0] = '\0';
    }
    return f;
}

void free_file(File* f) {
    if (!f) return;
    if (f->content) free(f->content);
    free(f);
}

void free_dir_recursive(Directory* d) {
    if (!d) return;
    for (int i=0;i<d->dir_count;i++) {
        free_dir_recursive(d->subdirs[i]);
    }
    for (int i=0;i<d->file_count;i++) {
        free_file(d->files[i]);
    }
    free(d);
}

void print_path_recursive(Directory* d) {
    if (d->parent == NULL) { printf("/"); return; }
    print_path_recursive(d->parent);
    printf("%s/", d->name);
}

// ---------- Virtual disk save/load ----------
void save_dir_to_file(FILE* f, Directory* dir, const char* path) {
    char fullpath[1024];
    for (int i=0;i<dir->dir_count;i++) {
        snprintf(fullpath, sizeof(fullpath), "%s%s/", path, dir->subdirs[i]->name);
        fprintf(f, "DIR %s\n", fullpath);
        save_dir_to_file(f, dir->subdirs[i], fullpath);
    }
    for (int i=0;i<dir->file_count;i++) {
        snprintf(fullpath, sizeof(fullpath), "%s%s", path, dir->files[i]->name);
        fprintf(f, "FILE %s\n", fullpath);
        if (dir->files[i]->content && strlen(dir->files[i]->content) > 0) {
            fprintf(f, "%s", dir->files[i]->content);
            if (dir->files[i]->content[strlen(dir->files[i]->content)-1] != '\n')
                fprintf(f, "\n");
        }
        fprintf(f, "END\n");
    }
}

void save_filesystem() {
    FILE* f = fopen(DISK_FILE, "w");
    if (!f) {
        printf("Error: could not write disk file.\n");
        return;
    }
    save_dir_to_file(f, root, "/");
    fclose(f);
}

Directory* find_or_create_dir_by_path(const char* path) {
    if (!path || path[0] == '\0') return root;
    if (strcmp(path, "/") == 0) return root;
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    // remove leading '/'
    char* p = tmp;
    if (p[0] == '/') p++;
    char* token = strtok(p, "/");
    Directory* cur = root;
    while (token) {
        int found = 0;
        for (int i=0;i<cur->dir_count;i++) {
            if (strcmp(cur->subdirs[i]->name, token) == 0) {
                cur = cur->subdirs[i];
                found = 1;
                break;
            }
        }
        if (!found) {
            Directory* nd = create_dir(token, cur);
            cur->subdirs[cur->dir_count++] = nd;
            cur = nd;
        }
        token = strtok(NULL, "/");
    }
    return cur;
}

void load_filesystem() {
    FILE* f = fopen(DISK_FILE, "r");
    if (!f) return; // no disk yet
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "DIR ", 4) == 0) {
            char path[1024];
            sscanf(line + 4, "%[^\n]", path);
            find_or_create_dir_by_path(path);
        } else if (strncmp(line, "FILE ", 5) == 0) {
            char path[1024];
            char content[MAX_CONTENT];
            content[0] = '\0';
            sscanf(line + 5, "%[^\n]", path);
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "END", 3) == 0) break;
                if (strlen(content) + strlen(line) + 1 < sizeof(content))
                    strcat(content, line);
            }
            // split path into dir + filename
            char *last = strrchr(path, '/');
            if (!last) continue;
            char filename[256];
            strcpy(filename, last+1);
            *last = '\0';
            Directory* dir = find_or_create_dir_by_path((strlen(path)>0) ? path : "/");
            File* nf = create_file(filename, content);
            dir->files[dir->file_count++] = nf;
        }
    }
    fclose(f);
    //printf("Virtual disk loaded.\n");
}

// ---------- Filesystem commands ----------
void list_dir() {
    printf("Directories:\n");
    for (int i=0;i<current_dir->dir_count;i++) {
        printf("  [DIR] %s\n", current_dir->subdirs[i]->name);
    }
    printf("Files:\n");
    for (int i=0;i<current_dir->file_count;i++) {
        printf("  %s\n", current_dir->files[i]->name);
    }
}

void cmd_cd(const char* name) {
    if (strcmp(name, "..") == 0) {
        if (current_dir->parent) current_dir = current_dir->parent;
        else printf("Already at root.\n");
        return;
    }
    for (int i=0;i<current_dir->dir_count;i++) {
        if (strcmp(current_dir->subdirs[i]->name, name) == 0) {
            current_dir = current_dir->subdirs[i];
            return;
        }
    }
    printf("Directory not found.\n");
}

void cmd_back() {
    if (current_dir->parent) current_dir = current_dir->parent;
    else printf("Already at root.\n");
}

void cmd_mkdir(const char* name) {
    if (current_dir->dir_count >= MAX_DIRS) { printf("Max dirs reached.\n"); return; }
    Directory* nd = create_dir(name, current_dir);
    current_dir->subdirs[current_dir->dir_count++] = nd;
    save_filesystem();
    printf("Directory '%s' created.\n", name);
}

void cmd_write(const char* name) {
    if (current_dir->file_count >= MAX_FILES) { printf("Max files reached here.\n"); return; }
    printf("Enter file content. Type 'END' on its own line to finish.\n");
    char buffer[MAX_CONTENT];
    buffer[0] = '\0';
    char line[512];
    // consume newline left by scanf in caller if any
    int c = getchar();
    if (c != '\n' && c != EOF) ungetc(c, stdin);
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, "END\n", 4) == 0 || strncmp(line, "END\r\n", 5) == 0) break;
        if (strlen(buffer) + strlen(line) + 1 < sizeof(buffer))
            strcat(buffer, line);
    }
    File* nf = create_file(name, buffer);
    current_dir->files[current_dir->file_count++] = nf;
    save_filesystem();
    printf("File '%s' created.\n", name);
}

void cmd_cat(const char* name) {
    for (int i=0;i<current_dir->file_count;i++) {
        if (strcmp(current_dir->files[i]->name, name) == 0) {
            printf("---- %s ----\n", name);
            if (current_dir->files[i]->content && strlen(current_dir->files[i]->content)>0)
                printf("%s", current_dir->files[i]->content);
            else
                printf("(empty)\n");
            printf("---- end ----\n");
            return;
        }
    }
    printf("File not found.\n");
}

void cmd_rm(const char* name) {
    for (int i=0;i<current_dir->file_count;i++) {
        if (strcmp(current_dir->files[i]->name, name) == 0) {
            free_file(current_dir->files[i]);
            for (int j=i;j<current_dir->file_count-1;j++) current_dir->files[j]=current_dir->files[j+1];
            current_dir->file_count--;
            save_filesystem();
            printf("File '%s' deleted.\n", name);
            return;
        }
    }
    printf("File not found.\n");
}

void delete_dir_node(Directory* node) {
    // recursively free children, but do NOT free 'node' pointer caller will manage if needed
    for (int i=0;i<node->dir_count;i++) {
        delete_dir_node(node->subdirs[i]);
        free(node->subdirs[i]);
        node->subdirs[i] = NULL;
    }
    for (int i=0;i<node->file_count;i++) {
        free_file(node->files[i]);
        node->files[i] = NULL;
    }
    node->dir_count = 0;
    node->file_count = 0;
}

void cmd_rmdir(const char* name) {
    for (int i=0;i<current_dir->dir_count;i++) {
        if (strcmp(current_dir->subdirs[i]->name, name) == 0) {
            // free sub-tree
            delete_dir_node(current_dir->subdirs[i]);
            free(current_dir->subdirs[i]);
            for (int j=i;j<current_dir->dir_count-1;j++) current_dir->subdirs[j]=current_dir->subdirs[j+1];
            current_dir->dir_count--;
            save_filesystem();
            printf("Directory '%s' and all contents removed.\n", name);
            return;
        }
    }
    printf("Directory not found.\n");
}

void cmd_clear() {
    for (int i=0;i<50;i++) printf("\n");
    printf("[screen cleared]\n");
}

void cmd_wipe() {
    printf("⚠️  Are you sure you want to wipe ALL user data? This cannot be undone (type 'yes' to confirm): ");
    char confirm[16];
    scanf("%15s", confirm);
    if (strcmp(confirm, "yes") != 0) { printf("Wipe cancelled.\n"); return; }
    // remove everything under root but keep the root directory itself
    for (int i=0;i<root->dir_count;i++) {
        delete_dir_node(root->subdirs[i]);
        free(root->subdirs[i]);
        root->subdirs[i] = NULL;
    }
    for (int i=0;i<root->file_count;i++) {
        free_file(root->files[i]);
        root->files[i] = NULL;
    }
    root->dir_count = 0;
    root->file_count = 0;
    current_dir = root;
    // remove any registered installed apps
    for (int i=0;i<app_count;i++) {
        if (!apps[i].builtin) {
            // shift left to drop
            for (int j=i;j<app_count-1;j++) apps[j]=apps[j+1];
            app_count--;
            i--;
        }
    }
    save_filesystem();
    printf("All user data wiped. Kernel intact.\n");
}

// ---------- App system ----------
void register_app(const char* name, const char* desc, const char* code, int builtin) {
    strncpy(apps[app_count].name, name, sizeof(apps[app_count].name)-1);
    strncpy(apps[app_count].desc, desc, sizeof(apps[app_count].desc)-1);
    apps[app_count].desc[sizeof(apps[app_count].desc)-1]='\0';
    if (code) strncpy(apps[app_count].code, code, sizeof(apps[app_count].code)-1);
    apps[app_count].code[sizeof(apps[app_count].code)-1]='\0';
    apps[app_count].builtin = builtin;
    app_count++;
}

void init_builtin_apps() {
    register_app("calculator", "Interactive calculator (+ - * /)", "BUILTIN_CALC", 1);
    register_app("notepad", "Notepad (saves as a file in current dir)", "BUILTIN_NOTEPAD", 1);
    register_app("numbergame", "Number Guess Game (1-100)", "BUILTIN_NUMBERGAME", 1);
    register_app("about", "About GR4V1TYOS", "BUILTIN_ABOUT", 1);
}

void load_installed_apps_from_vfs() {
    // find /apps directory if it exists
    Directory* appdir = find_or_create_dir_by_path("/apps");
    for (int i=0;i<appdir->file_count;i++) {
        File* f = appdir->files[i];
        if (!f) continue;
        // consider files ending in .savapp
        const char* ext = strrchr(f->name, '.');
        if (!ext || strcmp(ext, ".savapp") != 0) continue;
        // parse content: APP_NAME=..., APP_DESC=..., CODE=... (CODE can be multi-line until ENDAPP)
        char *copy = strdup(f->content ? f->content : "");
        char *line = strtok(copy, "\n");
        char name[64]="", desc[256]="", code[MAX_CONTENT]="";
        while (line) {
            if (strncmp(line, "APP_NAME=",9) == 0) strncpy(name, line+9, sizeof(name)-1);
            else if (strncmp(line, "APP_DESC=",9) == 0) strncpy(desc, line+9, sizeof(desc)-1);
            else if (strncmp(line, "CODE=",5) == 0) {
                // the rest of this line after CODE= is appended; also append subsequent lines until maybe ENDAPP or EOF
                strncat(code, line+5, sizeof(code)-strlen(code)-1);
                strncat(code, "\n", sizeof(code)-strlen(code)-1);
                // append remaining lines until ENDAPP or end of copy
                char *next = strtok(NULL, "\n");
                while (next) {
                    if (strcmp(next, "ENDAPP") == 0) break;
                    strncat(code, next, sizeof(code)-strlen(code)-1);
                    strncat(code, "\n", sizeof(code)-strlen(code)-1);
                    next = strtok(NULL, "\n");
                }
                break;
            }
            line = strtok(NULL, "\n");
        }
        free(copy);
        if (strlen(name)>0) register_app(name, desc, code, 0);
    }
}

void show_apps_command() {
    printf("Installed and built-in apps:\n");
    for (int i=0;i<app_count;i++) {
        printf("  %s - %s%s\n", apps[i].name, apps[i].desc, apps[i].builtin ? " [built-in]" : "");
    }
}

void app_builtin_calculator() {
    double a,b;
    char op;
    printf("Calculator - enter: <num> <op> <num>  (e.g. 5 * 3)\n");
    if (scanf("%lf %c %lf", &a, &op, &b) != 3) { printf("Invalid input.\n"); int c=getchar(); if (c!='\n') ; return; }
    double res = 0;
    if (op=='+') res = a+b;
    else if (op=='-') res = a-b;
    else if (op=='*') res = a*b;
    else if (op=='/') {
        if (b==0) { printf("Error: divide by zero.\n"); return; }
        res = a/b;
    } else { printf("Unknown operator.\n"); return; }
    printf("Result: %.6g\n", res);
}

void app_builtin_notepad() {
    char filename[128];
    printf("Notepad - enter filename to save in current directory: ");
    scanf("%127s", filename);
    // consume leftover newline
    int c = getchar();
    if (c != '\n' && c != EOF) ungetc(c, stdin);
    printf("Enter text lines. Type 'END' on its own line to finish.\n");
    char buffer[MAX_CONTENT];
    buffer[0] = '\0';
    char line[512];
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, "END\n", 4) == 0 || strncmp(line, "END\r\n", 5) == 0) break;
        if (strlen(buffer) + strlen(line) + 1 < sizeof(buffer))
            strcat(buffer, line);
    }
    // if file with same name exists in current_dir, overwrite
    for (int i=0;i<current_dir->file_count;i++) {
        if (strcmp(current_dir->files[i]->name, filename) == 0) {
            free(current_dir->files[i]->content);
            current_dir->files[i]->content = (char*)malloc(strlen(buffer)+1);
            strcpy(current_dir->files[i]->content, buffer);
            save_filesystem();
            printf("File '%s' overwritten.\n", filename);
            return;
        }
    }
    File* nf = create_file(filename, buffer);
    current_dir->files[current_dir->file_count++] = nf;
    save_filesystem();
    printf("File '%s' saved.\n", filename);
}

void app_builtin_numbergame() {
    srand((unsigned)time(NULL));
    int target = rand()%100 + 1;
    int guess = 0;
    int tries = 0;
    printf("Number Guess Game! Guess a number from 1 to 100.\n");
    while (1) {
        printf("Enter guess: ");
        if (scanf("%d", &guess) != 1) { printf("Invalid. Try again.\n"); int c=getchar(); if (c!='\n') ; continue; }
        tries++;
        if (guess > target) printf("Too high!\n");
        else if (guess < target) printf("Too low!\n");
        else { printf("Correct! You took %d tries.\n", tries); break; }
    }
}

void app_builtin_about() {
    printf("GR4V1TYOS Virtual Shell v4.0\n");
    printf("Features: Virtual filesystem, autosave, app library, app install/uninstall, wipe, rmdir, notepad, calculator, number game.\n");
    printf("All operations are sandboxed in the virtual filesystem.\n");
}

App* find_app_by_name(const char* name) {
    for (int i=0;i<app_count;i++) if (strcmp(apps[i].name, name) == 0) return &apps[i];
    return NULL;
}

void run_app_command(const char* name) {
    App* a = find_app_by_name(name);
    if (!a) { printf("App '%s' not found.\n", name); return; }
    if (a->builtin) {
        if (strcmp(a->code, "BUILTIN_CALC")==0) app_builtin_calculator();
        else if (strcmp(a->code, "BUILTIN_NOTEPAD")==0) app_builtin_notepad();
        else if (strcmp(a->code, "BUILTIN_NUMBERGAME")==0) app_builtin_numbergame();
        else if (strcmp(a->code, "BUILTIN_ABOUT")==0) app_builtin_about();
        else printf("Builtin app stub.\n");
    } else {
        // installed app: simple "interpreter": if code starts with "PRINT:" print rest; if "RUN_CMD:" we can interpret known commands.
        if (strncmp(a->code, "PRINT:", 6) == 0) {
            printf("%s\n", a->code + 6);
        } else if (strncmp(a->code, "SCRIPT:NOTEPAD", 14) == 0) {
            // notepad-like script: create a file with given name after SCRIPT:NOTEPAD <filename>
            char tmp[256];
            strncpy(tmp, a->code+14, sizeof(tmp)-1);
            tmp[sizeof(tmp)-1] = '\0';
            // trim whitespace
            char *p = tmp;
            while (*p == ' ' || *p == '\t') p++;
            if (strlen(p)==0) { printf("Installed notepad missing filename.\n"); return; }
            // open interactive input and save into file p in current dir
            printf("Installed notepad saving to '%s' in current directory.\n", p);
            printf("Enter text lines. Type 'END' on its own line to finish.\n");
            char buffer[MAX_CONTENT]; buffer[0]='\0';
            char line[512];
            int ch = getchar(); if (ch != '\n' && ch != EOF) ungetc(ch, stdin);
            while (1) {
                if (!fgets(line, sizeof(line), stdin)) break;
                if (strncmp(line, "END\n", 4) == 0 || strncmp(line, "END\r\n", 5) == 0) break;
                if (strlen(buffer) + strlen(line) + 1 < sizeof(buffer))
                    strcat(buffer, line);
            }
            // save file
            for (int i=0;i<current_dir->file_count;i++) {
                if (strcmp(current_dir->files[i]->name, p) == 0) {
                    free(current_dir->files[i]->content);
                    current_dir->files[i]->content = (char*)malloc(strlen(buffer)+1);
                    strcpy(current_dir->files[i]->content, buffer);
                    save_filesystem();
                    printf("File '%s' overwritten.\n", p);
                    return;
                }
            }
            File* nf = create_file(p, buffer);
            current_dir->files[current_dir->file_count++] = nf;
            save_filesystem();
            printf("File '%s' saved.\n", p);
        } else {
            // fallback: print the code block as output (safe)
            printf("--- App Output ---\n%s\n--- End ---\n", a->code);
        }
    }
}

void install_app_command(const char* packname) {
    // Known package installer: currently supports "hello" and "simple-notepad"
    Directory* appdir = find_or_create_dir_by_path("/apps");
    // ensure not already present
    char targetname[128];
    snprintf(targetname, sizeof(targetname), "%s.savapp", packname);
    for (int i=0;i<appdir->file_count;i++) {
        if (strcmp(appdir->files[i]->name, targetname) == 0) {
            printf("Package already installed.\n"); return;
        }
    }
    char content[MAX_CONTENT];
    if (strcmp(packname, "hello")==0) {
        snprintf(content, sizeof(content),
            "APP_NAME=hello\nAPP_DESC=Simple Hello App\nCODE=PRINT:Hello from installed Hello App!\nENDAPP\n");
    } else if (strcmp(packname, "simple-notepad")==0) {
        snprintf(content, sizeof(content),
            "APP_NAME=snotepad\nAPP_DESC=Simple installed notepad (saves to given filename)\nCODE=SCRIPT:NOTEPAD default_note.txt\nENDAPP\n");
    } else {
        printf("Unknown package '%s'. Known: hello, simple-notepad\n", packname);
        return;
    }
    File* nf = create_file(targetname, content);
    appdir->files[appdir->file_count++] = nf;
    save_filesystem();
    // register app immediately
    // reload installed apps (simple approach: clear non-builtins then reload)
    // remove existing non-builtins from apps array
    for (int i=0;i<app_count;i++) {
        if (!apps[i].builtin) {
            for (int j=i;j<app_count-1;j++) apps[j]=apps[j+1];
            app_count--;
            i--;
        }
    }
    load_installed_apps_from_vfs();
    printf("Package '%s' installed.\n", packname);
}

void uninstall_app_command(const char* appname) {
    Directory* appdir = find_or_create_dir_by_path("/apps");
    // find corresponding file by scanning .savapp files and checking APP_NAME
    for (int i=0;i<appdir->file_count;i++) {
        File* f = appdir->files[i];
        if (!f) continue;
        if (strstr(f->name, ".savapp")) {
            // parse APP_NAME line
            char *copy = strdup(f->content ? f->content : "");
            char *line = strtok(copy, "\n");
            char name[128]="";
            while (line) {
                if (strncmp(line, "APP_NAME=",9)==0) { strncpy(name, line+9, sizeof(name)-1); break; }
                line = strtok(NULL, "\n");
            }
            free(copy);
            if (strlen(name)>0 && strcmp(name, appname)==0) {
                // delete file
                free_file(f);
                for (int j=i;j<appdir->file_count-1;j++) appdir->files[j]=appdir->files[j+1];
                appdir->file_count--;
                save_filesystem();
                // remove from apps registry
                for (int k=0;k<app_count;k++) {
                    if (!apps[k].builtin && strcmp(apps[k].name, appname)==0) {
                        for (int j=k;j<app_count-1;j++) apps[j]=apps[j+1];
                        app_count--;
                        break;
                    }
                }
                printf("App '%s' uninstalled.\n", appname);
                return;
            }
        }
    }
    printf("Installed app '%s' not found.\n", appname);
}

void appinfo_command(const char* appname) {
    App* a = find_app_by_name(appname);
    if (!a) { printf("App not found.\n"); return; }
    printf("Name: %s\nDesc: %s\nType: %s\n", a->name, a->desc, a->builtin ? "built-in":"installed");
    if (!a->builtin) {
        printf("Code preview:\n%s\n", a->code);
    }
}

// ---------- Shell and main ----------
void print_help() {
    printf("Available commands:\n");
    printf(" help                - show this help\n");
    printf(" ls                  - list contents of current directory\n");
    printf(" cd <dir>            - change directory\n");
    printf(" back                - go up one directory\n");
    printf(" mkdir <name>        - create directory\n");
    printf(" rmdir <name>        - delete directory and its contents\n");
    printf(" write <file>        - create/write a file (use END to finish)\n");
    printf(" cat <file>          - show file contents\n");
    printf(" rm <file>           - delete file\n");
    printf(" clear               - clear virtual screen\n");
    printf(" wipe                - delete ALL user data (keeps kernel)\n");
    printf(" apps                - list apps (built-in + installed)\n");
    printf(" run <app>           - run an app\n");
    printf(" install <pkg>       - install package (hello, simple-notepad)\n");
    printf(" uninstall <app>     - uninstall installed app\n");
    printf(" appinfo <app>       - show info about an app\n");
    printf(" exit                - exit GR4V1TYOS (auto-saved)\n");
}

int main() {
    // init root
    root = create_dir("/", NULL);
    current_dir = root;

    // load FS
    load_filesystem();

    // init builtin apps
    init_builtin_apps();

    // load installed apps from /apps in vfs
    load_installed_apps_from_vfs();

    printf("Welcome to GR4V1TYOS v4.0\nType 'help' for commands.\n");

    char cmd[128];
    while (1) {
        printf("GR4V1TYOS:");
        print_path_recursive(current_dir);
        printf("> ");
        if (scanf("%127s", cmd) != 1) break;

        if (strcmp(cmd, "help")==0) print_help();
        else if (strcmp(cmd, "ls")==0) list_dir();
        else if (strcmp(cmd, "cd")==0) {
            char arg[256]; if (scanf("%255s", arg)!=1) { printf("cd needs an argument.\n"); continue; }
            cmd_cd(arg);
        }
        else if (strcmp(cmd, "back")==0) cmd_back();
        else if (strcmp(cmd, "mkdir")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("mkdir needs a name.\n"); continue; }
            cmd_mkdir(arg);
        }
        else if (strcmp(cmd, "rmdir")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("rmdir needs a name.\n"); continue; }
            cmd_rmdir(arg);
        }
        else if (strcmp(cmd, "write")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("write needs filename.\n"); continue; }
            // write handles its own newline consumption
            cmd_write(arg);
        }
        else if (strcmp(cmd, "cat")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("cat needs filename.\n"); continue; }
            cmd_cat(arg);
        }
        else if (strcmp(cmd, "rm")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("rm needs filename.\n"); continue; }
            cmd_rm(arg);
        }
        else if (strcmp(cmd, "clear")==0) cmd_clear();
        else if (strcmp(cmd, "wipe")==0) cmd_wipe();
        else if (strcmp(cmd, "apps")==0) show_apps_command();
        else if (strcmp(cmd, "run")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("run needs appname.\n"); continue; }
            run_app_command(arg);
        }
        else if (strcmp(cmd, "install")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("install needs packagename.\n"); continue; }
            install_app_command(arg);
        }
        else if (strcmp(cmd, "uninstall")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("uninstall needs appname.\n"); continue; }
            uninstall_app_command(arg);
        }
        else if (strcmp(cmd, "appinfo")==0) {
            char arg[128]; if (scanf("%127s", arg)!=1) { printf("appinfo needs appname.\n"); continue; }
            appinfo_command(arg);
        }
        else if (strcmp(cmd, "exit")==0) {
            save_filesystem();
            printf("Exiting GR4V1TYOS... (filesystem saved)\n");
            break;
        }
        else {
            printf("Unknown command: %s (type 'help')\n", cmd);
        }
    }

    // cleanup on exit
    free_dir_recursive(root);
    return 0;
}
