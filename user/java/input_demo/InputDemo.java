// 控制台输入 demo：readLine 回显
public class InputDemo {
    public static void main(String[] args) {
        Rmiku.IO.printStr("what's your name?");
        String name = Rmiku.IO.readLine();
        Rmiku.IO.printStr("you typed:");
        Rmiku.IO.printStr(name);

        Rmiku.IO.printStr("press any key:");
        int c = Rmiku.IO.readChar();
        Rmiku.IO.printStr("key code:");
        Rmiku.IO.printInt(c);
    }
}
