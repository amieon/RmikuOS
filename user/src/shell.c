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



#define MAX_DIRS 8
#define DIR_LEN 64
static char search_dirs[MAX_DIRS][DIR_LEN];
static int num_dirs = 0;


static void load_search_dirs(void) {
    num_dirs = 0;

    int fd = open("/etc/path", O_RDONLY);
    if (fd < 0) {

        copy_str(search_dirs[num_dirs++], "/bin/", DIR_LEN);
        copy_str(search_dirs[num_dirs++], "/tests/", DIR_LEN);
        return;
    }


    char buf[256];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        isize n = read(fd, buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';
    close(fd);


    int start = 0;
    for (int i = 0; i <= total; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            if (i > start && num_dirs < MAX_DIRS) {

                int len = i - start;
                if (len > DIR_LEN - 2) len = DIR_LEN - 2;
                int k = 0;
                for (int j = start; j < start + len; j++) {
                    search_dirs[num_dirs][k++] = buf[j];
                }

                if (k > 0 && search_dirs[num_dirs][k-1] != '/') {
                    search_dirs[num_dirs][k++] = '/';
                }
                search_dirs[num_dirs][k] = '\0';
                num_dirs++;
            }
            start = i + 1;
        }
    }


    if (num_dirs == 0) {
        copy_str(search_dirs[num_dirs++], "/bin/", DIR_LEN);
        copy_str(search_dirs[num_dirs++], "/tests/", DIR_LEN);
    }
}

static void build_exec_path(const char *prefix, const char *cmd, char *out, int out_size) {
    if (cmd[0] == '/') {
        int i = 0;
        while (cmd[i] && i < out_size - 1) {
            out[i] = cmd[i];
            i++;
        }
        out[i] = 0;
        return;
    }

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


    if (argv[0][0] == '/') {
        exec_with_args(argv[0], &args);
        puts("exec failed: ");
        puts(argv[0]);
        puts("\n");
        return;
    }

    for (int d = 0; d < num_dirs; d++) {
        char path[96];
        build_exec_path(search_dirs[d], argv[0], path, sizeof(path));  
        exec_with_args(path, &args);

    }

    puts("command not found: ");
    puts(argv[0]);
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

        //puts("[shell] child exit code ");
        //put_int(code);
        //puts("\n");
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
    int append = 0;

    for(int i=0;line[i] !=0;++i){
        len++;
        if(line[i] == '<'){
            input_cnt++;
            input_begin = line + i;

        }
        if(line[i] == '>'){
            output_cnt++;
            output_begin = line + i;
            if(line[i+1] == '>'){
                ++i;
                append = 1;
            }
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
        input_end = line + len +1;
    if(output_begin != 0 && output_end == 0)
        output_end = line + len +1;


    if(input_cnt == 1 && output_cnt == 1) {

        for(int i=0;i<input_end-input_begin;++i) {
            input[i] = input_begin[i];
        }
        input[input_end-input_begin]=0;
        for(int i=0;i<output_end-output_begin;++i) {
            output[i] = output_begin[i];
        }
        output[output_end-output_begin]=0;

        if (input_end < output_begin) {
            if(output_end!=line + len +1)
            remove_substring(line, output_begin, output_end);
            if(output_end!=line + len +1)
            remove_substring(line, input_begin, input_end);
        } else {
            if(input_end!=line + len +1)
            remove_substring(line, input_begin, input_end);
            if(output_end!=line + len +1)
            remove_substring(line, output_begin, output_end);
        }
        line[len - (output_end-output_begin) - (input_end-input_begin)] = '\0';
    }else if(input_cnt == 1){

        for(int i=0;i<input_end-input_begin;++i) {
            input[i] = input_begin[i];
        }
        input[input_end-input_begin]=0;

        output[0] = 0;
        if(input_end!=line + len +1)
        remove_substring(line, input_begin, input_end);
        line[len -(input_end-input_begin)] = '\0';
    }else if(output_cnt == 1){

        for(int i=0;i<output_end-output_begin;++i) {
            output[i] = output_begin[i];
        }
        output[output_end-output_begin]=0;
        input[0] = 0;
        if(output_end!=line + len +1)
        remove_substring(line, output_begin, output_end);
        line[len - (output_end-output_begin)] = '\0';
    }
    trim2(line);
    trim2(output);
    trim2(input);
    return append;
}
static void run_redirectline(char *line){
    char input[LINE_SIZE], output[LINE_SIZE];
    int status = handle_line(line,input,output);
    if(status < 0)return;
    char *argv[MAX_ARGC];
    int argc = parse_args(line, argv, MAX_ARGC);

    int pid = fork();
    if(pid == 0){
        if(input[0] != 0){
            isize fd = open(input, O_RDONLY);
            if(fd < 0){
                uprintf("Can not open %s, please check whether it exists\n",input);
                exit(1);
            }
            dup2(fd,0);
            close(fd);
        }   
        if(output[0] != 0){
            isize fd;
            if(status == 1)
                fd = open(output, O_CREAT|O_APPEND|O_WRONLY);
            else
                fd = open(output, O_CREAT|O_TRUNC|O_WRONLY);
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
    load_search_dirs();

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


        if (streq(argv[0], "pwd")) {
            int code = builtin_pwd();
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "cd")) {
            int code = builtin_cd(argc, argv);
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "mkdir")) {
            int code = builtin_mkdir(argc, argv);
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "touch")) {
            int code = builtin_create(argc, argv);
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "rm")) {
            int code = builtin_rm(argc, argv);
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "rmdir")) {
            int code = builtin_rmdir(argc, argv);
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        if (streq(argv[0], "shutdown")) {
            puts("bye bye~\n");
            shutdown();
           // puts("[shell] builtin exit code ");
           //put_int(code);
           //puts("\n");
            continue;
        }

        run_external(argc, argv);
    }

    return 0;
}