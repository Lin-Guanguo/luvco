import threading
import datetime
import argparse
import subprocess
import queue
import pathlib
from pathlib import Path

luvco_project_path = Path.home().joinpath('rpc-dev/luvco')
build_path = luvco_project_path.join('build')
log_path = luvco_project_path.join('build/logs')

task_q = queue.Queue()

def task_runner(testname):
    try:
        pass
    except:
        pass


def main():
    parser = argparse.ArgumentParser(prog="run test", description="run test multi times and save log")
    parser.add_argument("test")
    parser.add_argument("-times", '-t', default=4, type=int)
    parser.add_argument("-parallel", '-p', default=2, type=int)
    args = parser.parse_args()

    test : str = args.test
    times : int = args.times
    parallel : int = args.parallel
    timeprefix = datetime.datetime.now().strftime('%m%d%H%M%S')

    for i in range(times):
        task_q.put(f'./logs/{timeprefix}-{test}-{i}.log')

    for i in range(parallel):
        t = threading.Thread(target=task_runner, args=(test))
        t.start()

    task_q.join()
    print("=========== all test over ============")


if __name__ == '__main__':
    main()