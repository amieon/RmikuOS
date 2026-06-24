#include "user.h"

#define LINE_SIZE 128
#define MAX_ARGC  8

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int read_line(char *buf, int max_len) {
    int len = 0;

    while (len < max_len - 1) {
        char ch = 0;
        isize n = read(0, &ch, 1);

        if (n <= 0) {
            continue;
        }

        if (ch == '\r') {
            ch = '\n';
        }

        if (ch == '\n') {
            puts("\n");
            break;
        }

        if (ch == 8 || ch == 127) {
            if (len > 0) {
                len--;
                write(1, "\b \b", 3);
            }
            continue;
        }

        buf[len++] = ch;
        write(1, &ch, 1);
    }

    buf[len] = 0;
    return len;
}

static int parse_args(char *line, char *argv[], int max_argc) {
    int argc = 0;
    int i = 0;

    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') {
            line[i] = 0;
            i++;
        }

        if (!line[i]) {
            break;
        }

        if (argc >= max_argc) {
            break;
        }

        argv[argc++] = &line[i];

        while (line[i] && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
    }

    return argc;
}



static void copy_dirent_name(struct dirent *d, char *out, int out_size) {
    int n = d->name_len;
    if (n > out_size - 1) {
        n = out_size - 1;
    }

    for (int i = 0; i < n; i++) {
        out[i] = d->name[i];
    }

    out[n] = 0;
}

static void join_path(const char *dir, const char *name, char *out, int out_size) {
    int pos = 0;

    if (dir[0] == '.' && dir[1] == 0) {
        for (int i = 0; name[i] && pos < out_size - 1; i++) {
            out[pos++] = name[i];
        }
        out[pos] = 0;
        return;
    }

    for (int i = 0; dir[i] && pos < out_size - 1; i++) {
        out[pos++] = dir[i];
    }

    if (pos > 0 && out[pos - 1] != '/' && pos < out_size - 1) {
        out[pos++] = '/';
    }

    for (int i = 0; name[i] && pos < out_size - 1; i++) {
        out[pos++] = name[i];
    }

    out[pos] = 0;
}



static void print_help(void) {
    puts("commands:\n");
    puts("  help\n");
    puts("  exit\n");
    puts("  ls [path]\n");
    puts("  cat <path>\n");
    puts("\n");
    puts("external commands are in /bin:\n");
    puts("  try: ls /bin\n");
    puts("  cd <path>\n");
    puts("  pwd\n");
}

static void print_dirent_name(struct dirent *d) {
    for (int i = 0; i < d->name_len; i++) {
        char ch = d->name[i];
        write(1, &ch, 1);
    }

    if (d->file_type == FILE_TYPE_DIR) {
        puts("/");
    }

    puts("\n");
}


static int builtin_pwd(void) {
    char buf[128];

    isize n = getcwd(buf, sizeof(buf));

    if (n < 0) {
        puts("pwd: getcwd failed\n");
        return 1;
    }

    puts(buf);
    puts("\n");
    return 0;
}

static int builtin_cd(int argc, char *argv[]) {
    const char *path = "/";

    if (argc >= 2) {
        path = argv[1];
    }

    if (chdir(path) < 0) {
        puts("cd: no such directory: ");
        puts(path);
        puts("\n");
        return 1;
    }

    return 0;
}

static int builtin_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        puts("mkdir: missing operand\n");
        return 1;
    }
    if (mkdir(argv[1]) < 0) {
        puts("mkdir: cannot create directory: ");
        puts(argv[1]);
        puts("\n");
        return 1;
    }
    return 0;
}

static int builtin_create(int argc, char *argv[]) {
    if (argc < 2) {
        puts("create: missing operand\n");
        return 1;
    }
    if (create(argv[1]) < 0) {
        puts("create: cannot create file: ");
        puts(argv[1]);
        puts("\n");
        return 1;
    }
    return 0;
}



static int builtin_ls(int argc, char *argv[]) {

    const char *path = ".";

    if (argc >= 2) {
        path = argv[1];
    }

    int fd = open(path);
    if (fd < 0) {
        puts("ls: cannot open ");
        puts(path);
        puts("\n");
        return 1;
    }

    struct dirent entries[8];

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {

            puts("ls: not a directory: ");
            puts(path);
            puts("\n");
            close(fd);
            return 1;
        }

        if (n == 0) {
            break;
        }

        int count = n / sizeof(struct dirent);

        for (int i = 0; i < count; i++) {
            char name[64];
            char full_path[128];
            struct stat st;

            copy_dirent_name(&entries[i], name, sizeof(name));
            join_path(path, name, full_path, sizeof(full_path));

            if (stat(full_path, &st) < 0) {
                puts("?       ");
                puts(name);
                puts("\n");
                continue;
            }

            if (st.file_type == STAT_TYPE_DIR) {
                puts("dir     ");
            } else if (st.file_type == STAT_TYPE_FILE) {
                puts("file    ");
            } else if (st.file_type == STAT_TYPE_CHAR) {
                puts("char    ");
            } else {
                puts("unknown ");
            }

            put_int(st.size);
            puts(" ");

            puts(name);
            if (st.file_type == STAT_TYPE_DIR) {
                puts("/");
            }
            puts("\n");
        }
    }

    close(fd);
    return 0;
}

static int builtin_cat(int argc, char *argv[]) {
    if (argc < 2) {
        puts("cat: missing path\n");
        return 1;
    }


    int ret = 0;

    for (int argi = 1; argi < argc; argi++) {
        const char *path = argv[argi];

        int fd = open(path);
        if (fd < 0) {
            puts("cat: cannot open ");
            puts(path);
            puts("\n");
            ret = 1;
            continue;
        }

        char buf[128];

        while (1) {
            isize n = read(fd, buf, sizeof(buf));

            if (n < 0) {
                puts("cat: read failed: ");
                puts(path);
                puts("\n");
                ret = 1;
                break;
            }

            if (n == 0) {
                break;
            }

            write(1, buf, n);
        }

        close(fd);
    }

    return ret;
}


static int builtin_rm(int argc, char *argv[]) {
    if (argc < 2) {
        puts("rm: missing operand\n");
        return 1;
    }


    int recursive = 0;
    int start = 1;
    if (streq(argv[1], "-r") || streq(argv[1], "-rf") || streq(argv[1], "-f")) {
        if (streq(argv[1], "-r") || streq(argv[1], "-rf")) recursive = 1;
        start = 2; 
    }

    int ret = 0;
    for (int i = start; i < argc; i++) {
        int r;
        if (recursive) {
            r = remove_recursive(argv[i]); 
        } else {
            r = unlink(argv[i]);         
        }
        if (r < 0) {
            puts("rm: cannot remove ");
            puts(argv[i]);
            puts("\n");
            ret = 1;
        }
    }
    return ret;
}

static int builtin_rmdir(int argc, char *argv[]) {
    if (argc < 2) { puts("rmdir: missing operand\n"); return 1; }
    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) { 
            puts("rmdir: failed to remove ");
            puts(argv[i]);
            puts("\n");
            ret = 1;
        }
    }
    return ret;
}

static void build_exec_path(const char *cmd, char *out, int out_size) {
    if (cmd[0] == '/') {
        int i = 0;
        while (cmd[i] && i < out_size - 1) {
            out[i] = cmd[i];
            i++;
        }
        out[i] = 0;
        return;
    }

    const char *prefix = "/bin/";
    int pos = 0;

    for (int i = 0; prefix[i] && pos < out_size - 1; i++) {
        out[pos++] = prefix[i];
    }

    for (int i = 0; cmd[i] && pos < out_size - 1; i++) {
        out[pos++] = cmd[i];
    }

    out[pos] = 0;
}

static void run_exec(int argc, char *argv[]){
    char path[96];

    build_exec_path(argv[0], path, sizeof(path));

    struct exec_args args;
    args.argc = argc;

    for (int i = 0; i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = 0;
        args.argv[i].len = 0;
    }

    for (int i = 0; i < argc && i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = argv[i];
        args.argv[i].len = strlen(argv[i]);
    }

    exec_with_args(path, &args);

    puts("exec failed: ");
    puts(path);
    puts("\n");
}

static void run_external(int argc, char *argv[]) {
    isize pid = fork();

    if (pid == 0) {
        run_exec(argc,argv);
        exit(1);
    } else if (pid > 0) {
        int code = -1;
        waitpid(pid, &code);

        puts("[shell] child exit code ");
        put_int(code);
        puts("\n");
    } else {
        puts("fork failed\n");
    }
}

static void run_pipeline(char *line){
    char * line2 = line;
    for(int i=0;line[i] != '\0';++i)
        if(line[i] == '|'){
            line[i] = '\0';
            line2 = line + i + 1;
            break;
        }
    
    char *argv1[MAX_ARGC];
    int argc1 = parse_args(line, argv1, MAX_ARGC);

    char *argv2[MAX_ARGC];
    int argc2 = parse_args(line2, argv2, MAX_ARGC);
    
    int fd[2];
    if (pipe(fd) < 0) { puts("pipe failed\n"); return; }
    if (argc1 == 0 || argc2 == 0) { puts("pipe: empty command\n"); return; }

    int pid1 = fork();
    if (pid1 == 0) {
        dup2(fd[1], 1);
        close(fd[0]);
        close(fd[1]);
        run_exec(argc1, argv1);
        exit(1);
    }

    int pid2 = fork();
    if (pid2 == 0) {
        dup2(fd[0], 0);
        close(fd[0]);
        close(fd[1]);
        run_exec(argc2, argv2);
        exit(1);
    }


    close(fd[0]);
    close(fd[1]);
    waitpid(pid1, 0);
    waitpid(pid2, 0);

}

void remove_substring(char *str, char *start, char *end) {
    if (str == 0 || start == 0 || end == 0 || start >= end) return;

    char *src = end;
    char *dst = start;

    while (*src != '\0') {
        *dst++ = *src++;
    }
    *dst = '\0';
}

int handle_line(char *line,char *input,char *output){
    int input_cnt = 0,output_cnt = 0;
    char *input_begin  = 0;
    char *input_end    = 0;
    char *output_begin = 0;
    char *output_end   = 0;
    int input_flag = 0,output_flag = 0;
    int len = 0;

    for(int i=0;line[i] !=0;++i){
        len++;
        if(line[i] == '<'){
            input_cnt++;
            input_begin = line + i;

        }
        if(line[i] == '>'){
            output_cnt++;
            output_begin = line + i;
        }

        if(line[i] == ' '){
            if(input_flag && input_begin != 0 && input_end == 0){
                input_end = line + i;
            }
            if(output_flag && output_begin != 0 && output_end == 0){
                output_end = line + i;
            }
        }else if(line[i] != '<' && line[i] != '>'){
            if(input_begin != 0 && input_end == 0){
                input_flag = 1;
            }
            if(output_begin != 0 && output_end == 0){
                output_flag = 1;
            }
        }
    }

    if(input_cnt > 1 || output_cnt > 1)
        return -1;
    if(input_begin != 0 && input_end == 0)
        input_end = line + len ;
    if(output_begin != 0 && output_end == 0)
        output_end = line + len ;


    if(input_cnt == 1 && output_cnt == 1) {
        *output_end = '\0';
        *input_end = '\0';
        *output_begin = ' ';
        *input_begin = ' ';

        for(int i=1;input_begin[i] != '\0';++i) {
            input[i - 1] = input_begin[i];
            input[i] = '\0';
        }
        for(int i=1;output_begin[i] != '\0';++i) {
            output[i - 1] = output_begin[i];
            output[i] = '\0';
        }

        if(output_end == line + len)*output_end = '\0';
        else *output_end = ' ';

        if(input_end == line + len)*input_end = '\0';
        else *input_end = ' ';

        if (input_end < output_begin) {
            remove_substring(line, output_begin, output_end);
            remove_substring(line, input_begin, input_end);
        } else {
            remove_substring(line, input_begin, input_end);
            remove_substring(line, output_begin, output_end);
        }
    }else if(input_cnt == 1){
        *input_end = '\0';
        *input_begin = ' ';

        for(int i=1;input_begin[i] != '\0';++i) {
            input[i - 1] = input_begin[i];
            input[i] = '\0';
        }
        output[0] = 0;

        if(input_end == line + len)*input_end = '\0';
        else *input_end = ' ';
        remove_substring(line, input_begin, input_end);
    }else if(output_cnt == 1){
        *output_end = '\0';
        *output_begin = ' ';

        for(int i=1;output_begin[i] != '\0';++i) {
            output[i - 1] = output_begin[i];
            output[i] = '\0';
        }
        input[0] = 0;

        if(output_end == line + len)*output_end = '\0';
        else *output_end = ' ';
        remove_substring(line, output_begin, output_end);
    }
    trim(line);
    trim(output);
    trim(input);
    return 0;
}
static void run_redirectline(char *line){
    char input[LINE_SIZE], output[LINE_SIZE];
    handle_line(line,input,output);
    char *argv[MAX_ARGC];
    int argc = parse_args(line, argv, MAX_ARGC);

    int pid = fork();
    if(pid == 0){
        if(input[0] != 0){
            isize fd = open(input);
            if(fd < 0){
                uprintf("Can not open %s, please check whether it exists\n",input);
                exit(1);
            }
            dup2(fd,0);
            close(fd);
        }   
        if(output[0] != 0){
            isize fd = open_create(output);
            if(fd < 0){
                uprintf("Can not open %s\n",output);
                exit(1);
            }
            dup2(fd,1);
            close(fd);
        }
        run_exec(argc,argv);
        exit(1);
    }else{
        waitpid(pid,0);
    }


}

static int has_pipe(char *s){
    for(int i=0;s[i] != '\0';++i)
        if(s[i] == '|')
            return 1;
    return 0;
}


static int has_redirect(char *s){
    for(int i=0;s[i] != '\0';++i)
        if(s[i] == '<' || s[i] == '>')
            return 1;
    return 0;
}

int main(void) {
    char line[LINE_SIZE];
    char *argv[MAX_ARGC];

    puts("\nRmikuOS shell\n");
    print_help();

    while (1) {
        char cwd_buf[128];

        puts("\n");
        if (getcwd(cwd_buf, sizeof(cwd_buf)) >= 0) {
            puts(cwd_buf);
        }
        puts(" $ ");

        int len = read_line(line, LINE_SIZE);

        if (len == 0) {
            continue;
        }

        int have_pipe = 0,have_redirect = 0;
        if (has_pipe(line)) {
            have_pipe = 1;      
        }
                
        if (has_redirect(line)) {
            have_redirect = 1;      
        }
        if(have_pipe && have_redirect){
            puts("Does not support both pipe and redirection simultaneously.\n");
            continue;
        }
        if(have_pipe){
            run_pipeline(line);
            continue;
        }
        if(have_redirect){
            run_redirectline(line);
            continue;
        }

        int argc = parse_args(line, argv, MAX_ARGC);

        if (argc == 0) {
            continue;
        }

        if (streq(argv[0], "help")) {
            print_help();
            continue;
        }

        if (streq(argv[0], "exit")) {
            puts("bye\n");
            return 0;
        }

        if (streq(argv[0], "ls")) {
            int code = builtin_ls(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "cat")) {
            int code = builtin_cat(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "pwd")) {
            int code = builtin_pwd();
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "cd")) {
            int code = builtin_cd(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "mkdir")) {
            int code = builtin_mkdir(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "touch")) {
            int code = builtin_create(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "rm")) {
            int code = builtin_rm(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "rmdir")) {
            int code = builtin_rmdir(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        run_external(argc, argv);
    }

    return 0;
}