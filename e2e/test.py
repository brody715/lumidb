import argparse
import re
import subprocess
import os
import pathlib
import logging
from dataclasses import dataclass
from tempfile import NamedTemporaryFile
from typing import Dict, List, Tuple

try:
    import coloredlogs
    coloredlogs.install(level=logging.INFO, fmt='[%(levelname)s] %(message)s')
except ImportError:
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)s] %(message)s')
    pass

logger = logging.getLogger(__name__)

script_path = pathlib.Path(os.path.dirname(__file__)).absolute()


def require(msg: str, value):
    if not value:
        raise ValueError(msg)
    return value


def run_shell(args: str, *, shell=True, check=True, capture_output=False, input: bytes = None, encoding: str = None, stdin=None, stdout=None, stderr=None):
    try:
        p = subprocess.run(args, shell=shell, check=check,
                           capture_output=capture_output, input=input, encoding=encoding, stdin=stdin, stderr=stderr, stdout=stdout)
        return p
    except subprocess.CalledProcessError as e:
        err = ''
        if e.stderr is not None:
            err = e.stderr.decode('utf-8')
        raise RuntimeError(
            f"Command '{args}' returned non-zero exit status {e.returncode}, err=\n", err) from None
    return


@dataclass
class TxtarFile:
    name: str
    content: str = ''


class Txtar:
    def __init__(self) -> None:
        self.files: List[TxtarFile] = []
        self.files_by_name: Dict[str, TxtarFile] = {}

    def add_file(self, file: TxtarFile):
        if file.name in self.files_by_name:
            raise ValueError(f"txtar: duplicated file.name={file.name}")

        self.files.append(file)
        self.files_by_name[file.name] = file

    def get_file(self, name: str) -> TxtarFile:
        return self.files_by_name[name]


def parse_txtar(content: str, strip_empty_line=False) -> Txtar:

    txtar = Txtar()
    cur_file = TxtarFile(name='__root__')

    re_header = re.compile(r"^-- (\S+) --$")
    for line in content.splitlines():
        line = line.strip()
        if strip_empty_line and len(line) == 0:
            continue

        m = re_header.match(line)
        if m:
            txtar.add_file(cur_file)
            cur_file = TxtarFile(name=m.group(1))
            continue
        cur_file.content += line + '\n'

    txtar.add_file(cur_file)
    return txtar


PROGRAM_ARGS = "./build/lumidb"


def do_diff(lhs: str, rhs: str) -> bool:

    with NamedTemporaryFile(delete=True) as lhs_file:
        lhs_file.write(lhs.encode("utf-8"))
        lhs_file.flush()
        p = run_shell(f"diff -u --color - {lhs_file.name}",
                      input=rhs.encode("utf-8"), check=False)

    return p.returncode != 0


def filter_query_content(query_content: str) -> str:
    lines = []
    for line in query_content.splitlines():
        if line.startswith("/*"):
            continue
        if line.startswith("--"):
            continue
        lines.append(line)
    return '\n'.join(lines)


def execute_query(query_content: str) -> str:

    def execute(lines: List[str]) -> str:
        if len(lines) == 0:
            return ""
        input = '\n'.join(lines)
        p = run_shell(f"{PROGRAM_ARGS}", check=True, capture_output=True,
                      input=input.encode("utf-8"))

        return p.stdout.decode("utf-8")

    lines = []
    out = ''
    for line in query_content.splitlines():
        if line.startswith("CMD "):
            out += execute(lines)
            lines = []
            cmd = line[4:]
            run_shell(cmd, shell=True, capture_output=True)
        else:
            lines.append(line)

    out += execute(lines)
    return out


def filter_golden_content(golden_content: str) -> str:
    lines = []
    for line in golden_content.splitlines():
        if line.startswith("#"):
            continue
        if len(line) == 0:
            continue
        lines.append(line)
    return '\n'.join(lines) + '\n'


def run_txtar_tests(args, txtar_name, txtar_path: pathlib.Path):

    includes = args.includes or ''
    includes = includes.split(',')
    includes = list(filter(lambda x: bool(x), includes))

    txtar = parse_txtar(txtar_path.read_text(), strip_empty_line=True)

    test_files: List[Tuple[TxtarFile, TxtarFile]] = []

    for file in txtar.files:
        if file.name.endswith(".in"):
            if len(includes) != 0 and file.name not in includes:
                continue

            golden_file_name = file.name.replace(".in", "") + ".golden"
            golden_file = txtar.get_file(golden_file_name)

            test_files.append((file, golden_file))

    # combine are test file in one files

    in_content = ''
    golden_content = ''

    for in_file, golden_file in test_files:
        in_content += filter_query_content(in_file.content) + "\n"
        golden_content += filter_golden_content(
            golden_file.content) + "\n"

    logger.info(f"test {txtar_name} starting")

    test_out = ''
    try:
        test_out = execute_query(in_content)
    except Exception as e:
        logger.error(
            f"failed to run test {txtar_name}, err={e}")
        return

    if args.debug:
        print("--- got ---")
        print(test_out)
        print("--- expected ---")
        print(golden_content)

    golden_out = filter_golden_content(golden_content)

    # has_diff = do_diff(golden_file.content, sql_out)
    has_diff = do_diff(golden_out, test_out)

    if has_diff:
        logger.error(f"test {txtar_name} failed")
    else:
        logger.info(f"test {txtar_name} ok")


ALL_TESTS = []

ALL_TESTS_MAP = {fn.__name__: fn for fn in ALL_TESTS}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", type=str, required=False, default='')
    parser.add_argument("--includes", type=str)
    parser.add_argument("--debug", action="store_true", default=False)

    return parser.parse_args()


def main():
    args = parse_args()

    cases = args.case.split(',')
    cases = list(filter(bool, cases))

    testdata_dir = (script_path / "data")

    if len(cases) > 0:
        for case in cases:
            case_func = ALL_TESTS_MAP.get(case)

            if case_func:
                case_func(args)
                return

            # try to find txtar file

            txtar_file = testdata_dir / f"{case}.txtar"

            if not txtar_file.exists():
                logger.error(f"no such case: {case}")
                return

            run_txtar_tests(args, case, txtar_file)

    else:
        for file in testdata_dir.glob("*.txtar"):
            run_txtar_tests(args, file.stem, file)


if __name__ == '__main__':
    main()
