import os
import sys

def count_lines_in_file(file_path):
    """统计单个文件的行数（二进制模式，按换行符分割）"""
    try:
        with open(file_path, 'rb') as f:
            # 逐行读取并计数，内存友好
            return sum(1 for _ in f)
    except (IOError, OSError) as e:
        print(f"无法读取文件 {file_path}: {e}", file=sys.stderr)
        return 0

def count_lines_in_directory(dir_path):
    """递归统计目录下所有文件的行数总和"""
    total_lines = 0
    file_count = 0
    for root, dirs, files in os.walk(dir_path):
        for file in files:
            file_path = os.path.join(root, file)
            # 跳过符号链接，避免重复或循环
            if os.path.islink(file_path):
                continue
            lines = count_lines_in_file(file_path)
            total_lines += lines
            file_count += 1
    return total_lines, file_count

def main():
    # 获取目标文件夹路径（默认为当前目录）
    if len(sys.argv) > 1:
        target_dir = sys.argv[1]
    else:
        target_dir = os.getcwd()

    if not os.path.isdir(target_dir):
        print(f"错误：'{target_dir}' 不是一个有效的目录。", file=sys.stderr)
        sys.exit(1)

    total_lines, file_count = count_lines_in_directory(target_dir)
    print(f"目录 '{target_dir}' 下共有 {file_count} 个文件，总行数为: {total_lines}")

if __name__ == "__main__":
    main()