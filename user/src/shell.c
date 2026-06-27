#include "user.h"

#define LINE_SIZE 128
#define MAX_ARGC  16


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
            i++;
        }
        if (!line[i]) {
            break;
        }
        if (line[i] == '#') {
            break;
        }
        if (argc >= max_argc) {
            break;
        }
        argv[argc++] = &line[i];
        int w = i;

        while (line[i] && line[i] != ' ' && line[i] != '\t') {
            char c = line[i];
            if (c == '\\') {
                i++;
                if (line[i]) {
                    line[w++] = line[i];
                    i++;
                }
            } else if (c == '"') {
                i++;  
                while (line[i] && line[i] != '"') {
                    if (line[i] == '\\') {
                        i++;
                        if (line[i]) {
                            line[w++] = line[i];
                            i++;
                        }
                    } else {
                        line[w++] = line[i++];
                    }
                }
                if (line[i] == '"') {
                    i++;   
                }
            } else if (c == '\'') {
                i++; 
                while (line[i] && line[i] != '\'') {
                    line[w++] = line[i++];
                }
                if (line[i] == '\'') {
                    i++; 
                }
            } else {
                line[w++] = c;
                i++;
            }
        }
        int had_sep = (line[i] == ' ' || line[i] == '\t');
        line[w] = '\0';
        if (had_sep) {
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
#define MAX_SEGMENTS 8    


struct segment {
    char *cmd_str;      
    char  infile[LINE_SIZE];  
    char  outfile[LINE_SIZE];
    int   append;   
};

static char *skip_spaces(char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}


static int parse_redirect(char *seg_str, struct segment *out) {
    out->cmd_str   = seg_str;
    out->infile[0]  = 0;
    out->outfile[0] = 0;
    out->append     = 0;

    int i = 0;   
    int w = 0; 
    while (seg_str[i]) {
        char c = seg_str[i];
        if (c == '"' || c == '\'') {
            char quote = c;
            seg_str[w++] = seg_str[i++];  
            while (seg_str[i] && seg_str[i] != quote) {
                if (quote == '"' && seg_str[i] == '\\' && seg_str[i+1]) {
                    seg_str[w++] = seg_str[i++];
                }
                seg_str[w++] = seg_str[i++];
            }
            if (seg_str[i] == quote) {
                seg_str[w++] = seg_str[i++];    
            }
            continue;
        }
        if (c == '\\') {
            seg_str[w++] = seg_str[i++];
            if (seg_str[i]) seg_str[w++] = seg_str[i++];
            continue;
        }

        if (c == '>' || c == '<') {
            int is_output = (c == '>');
            int append = 0;

            i++;
            if (is_output && seg_str[i] == '>') { 
                append = 1;
                i++;
            }
            i = (int)(skip_spaces(seg_str + i) - seg_str);

            if (seg_str[i] == 0 ||
                seg_str[i] == '>' || seg_str[i] == '<' || seg_str[i] == '|') {
                return -1;
            }


            char target[LINE_SIZE];
            int t = 0;
            while (seg_str[i] && seg_str[i] != ' ' && seg_str[i] != '\t' &&
                   seg_str[i] != '>' && seg_str[i] != '<' && seg_str[i] != '|') {
                if (t < LINE_SIZE - 1) target[t++] = seg_str[i];
                i++;
            }
            target[t] = 0;


            if (is_output) {
                copy_str(out->outfile, target, LINE_SIZE);
                out->append = append;
            } else {
                copy_str(out->infile, target, LINE_SIZE);
            }
            continue;
        }


        seg_str[w++] = seg_str[i++];
    }

    seg_str[w] = 0;   
    return 0;
}


static void apply_redirect_and_exec(struct segment *seg, int argc, char *argv[]) {
    if (seg->infile[0]) {
        isize fd = open(seg->infile, O_RDONLY);
        if (fd < 0) {
            uprintf("cannot open %s for input\n", seg->infile);
            exit(1);
        }
        dup2(fd, 0);
        close(fd);
    }
    if (seg->outfile[0]) {
        isize fd;
        if (seg->append)
            fd = open(seg->outfile, O_CREAT | O_APPEND | O_WRONLY);
        else
            fd = open(seg->outfile, O_CREAT | O_TRUNC | O_WRONLY);
        if (fd < 0) {
            uprintf("cannot open %s for output\n", seg->outfile);
            exit(1);
        }
        dup2(fd, 1);
        close(fd);
    }
    run_exec(argc, argv);
    exit(1);
}


static void run_line(char *line) {
    char *seg_strs[MAX_SEGMENTS];
    int   nseg = 0;


seg_strs[nseg++] = line;
{
    char quote = 0; 
    for (int i = 0; line[i]; i++) {
        char c = line[i];
 
        if (quote) {
            if (c == '\\' && quote == '"' && line[i+1]) {
                i++;   
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
 
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '\\' && line[i+1]) {
            i++;  
        } else if (c == '|') {
            line[i] = 0;
            if (nseg >= MAX_SEGMENTS) {
                puts("too many pipe segments\n");
                return;
            }
            seg_strs[nseg++] = line + i + 1;
        }
    }
}


    struct segment segs[MAX_SEGMENTS];
    for (int s = 0; s < nseg; s++) {
        if (parse_redirect(seg_strs[s], &segs[s]) < 0) {
            puts("syntax error in redirection\n");
            return;   
        }
    }


    static char *seg_argv[MAX_SEGMENTS][MAX_ARGC];
    int seg_argc[MAX_SEGMENTS];
    for (int s = 0; s < nseg; s++) {
        seg_argc[s] = parse_args(segs[s].cmd_str, seg_argv[s], MAX_ARGC);
        if (seg_argc[s] == 0) {
            puts("syntax error: empty command\n");
            return;
        }
    }

    int prev_read = -1;
    int pids[MAX_SEGMENTS];
    int npid = 0;

    for (int s = 0; s < nseg; s++) {
        int pipefd[2];
        int has_next = (s < nseg - 1);

        if (has_next) {
            if (pipe(pipefd) < 0) {
                puts("pipe failed\n");
                break;
            }
        }

        int pid = fork();
        if (pid == 0) {
            if (prev_read >= 0) {
                dup2(prev_read, 0);
            }

            if (has_next) {
                dup2(pipefd[1], 1);
            }

            if (prev_read >= 0) close(prev_read);
            if (has_next) {
                close(pipefd[0]);
                close(pipefd[1]);
            }

            apply_redirect_and_exec(&segs[s], seg_argc[s], seg_argv[s]);
            exit(1);
        } else if (pid > 0) {
            pids[npid++] = pid;
        } else {
            puts("fork failed\n");
        }

  
        if (prev_read >= 0) close(prev_read);
        if (has_next) {
            close(pipefd[1]);  
            prev_read = pipefd[0]; 
        } else {
            prev_read = -1;
        }
    }

    if (prev_read >= 0) close(prev_read);

    for (int i = 0; i < npid; i++) {
        waitpid(pids[i], 0);
    }
}

static int has_pipe_or_redirect(const char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '|' || s[i] == '<' || s[i] == '>') {
            return 1;
        }
    }
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
 
        if (has_pipe_or_redirect(line)) {
            run_line(line);
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
            builtin_pwd();
            continue;
        }
        if (streq(argv[0], "cd")) {
            builtin_cd(argc, argv);
            continue;
        }
        if (streq(argv[0], "mkdir")) {
            builtin_mkdir(argc, argv);
            continue;
        }
        if (streq(argv[0], "touch")) {
            builtin_create(argc, argv);
            continue;
        }
        if (streq(argv[0], "rm")) {
            builtin_rm(argc, argv);
            continue;
        }
        if (streq(argv[0], "rmdir")) {
            builtin_rmdir(argc, argv);
            continue;
        }
        if (streq(argv[0], "shutdown")) {
            puts("bye bye~\n");
            shutdown();
            continue;
        }

        run_external(argc, argv);
    }
 
    return 0;
}