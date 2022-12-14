from glob import glob
import os
import re
import sys

USAGE = """USAGE:
    {} [DIR]"""


def print_usage(name):
    print(USAGE.format(name))


def main():
    args = sys.argv
    if len(args) < 2 or not os.path.isdir(args[1]):
        print_usage(args[0])
        exit(1)

    # extract /* */ and //
    comment_pattern = "(/\*\*?((.|\s)*?)\*/|///?<?(.*))"
    comment_re = re.compile(comment_pattern)

    strip_pattern = re.compile("\n\s*\*? ?")
    xml_tag = re.compile("</?.*?>")
    include_guard = re.compile("\w*_HPP_")
    code_example = re.compile("```.*?```")
    doxygen_tag = re.compile(
        "(@brief|@t?param(\[(in|out|inout)\])?\s*\w*|@return|@note)")
    spaces = re.compile(" +")

    header_glob = os.path.join(args[1], "*.hpp")
    print("# `{}`".format(header_glob))
    print()
    for name in glob(header_glob):
        with open(name) as f:
            content = f.read()
            result = comment_re.findall(content)
            result = [x[3].strip() if x[3] else re.sub(strip_pattern, " ", x[1]).strip()
                      for x in result]
            result = filter(lambda x: not x.startswith("namespace "), result)
            result = filter(lambda x: not xml_tag.match(x), result)
            result = filter(lambda x: not include_guard.match(x), result)
            result = map(lambda x: re.sub(code_example, " ", x), result)
            result = map(lambda x: re.sub(doxygen_tag, "\n- ", x), result)
            result = map(lambda x: re.sub(spaces, " ", x), result)
            result = map(lambda x: x.strip(), result)
            result = filter(lambda x: x != "", result)

            print("## `{}`".format(name))
            print()
            for line in result:
                print(line)
            print()


if __name__ == "__main__":
    main()
