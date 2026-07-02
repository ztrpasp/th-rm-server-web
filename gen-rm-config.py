import os

def main():
    num = input("请输入AS号: ").strip()
    if not num.isdigit():
        print("输入不是数字！")
        return
    n = int(num)

    file_content = ""

    if n in [1, 2, 3, 4, 5]:
        file_content = """@
#0 10.0.1.1
#3 10.0.1.1
#4 10.0.1.1
#7 10.0.1.1
&
"""
    elif n in [10 ,11, 12, 13, 14, 15]:
        a = n % 10
        file_content = f"""@
#0 10.134.{a}.1
#3 10.134.{a}.1
#4 10.134.{a}.1
#7 10.134.{a}.1
&
"""
    elif n == 0:
        file_content = """@
#0 10.50.51.0
#3 10.50.51.0
#4 10.50.51.0
#7 10.50.51.0
&
"""
    else:
        print("输入不在要求范围内，不生成文件。")
        return

    with open("rm_config", "w", encoding="utf-8") as f:
        f.write(file_content)
    print("rm_config 文件已生成/覆盖。")

if __name__ == "__main__":
    main()
