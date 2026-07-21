// 进程 demo：fork + waitpid
public class ProcDemo {
    public static void main(String[] args) {
        Rmiku.IO.printStr("parent pid:");
        Rmiku.IO.printInt(Rmiku.Proc.getpid());

        int pid = Rmiku.Proc.fork();
        if (pid == 0) {
            // 子进程
            Rmiku.IO.printStr("child pid:");
            Rmiku.IO.printInt(Rmiku.Proc.getpid());
            Rmiku.Proc.sleep(10);
            Rmiku.IO.printStr("child exit with 42");
            Rmiku.Proc.exit(42);
        } else {
            // 父进程等子进程，打印退出码
            int code = Rmiku.Proc.waitpid(pid);
            Rmiku.IO.printStr("child exit code:");
            Rmiku.IO.printInt(code);
        }
    }
}
