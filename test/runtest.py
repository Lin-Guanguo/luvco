import threading
import datetime
import argparse
import subprocess
import queue
import os
import re
from pathlib import Path
import resource

luvco_project_path = Path.home().joinpath('rpc-dev/luvco')
build_path = luvco_project_path.joinpath('build')
log_path = luvco_project_path.joinpath('build/logs')

task_q = queue.Queue()
test_cmd : list


def unlimited_coredump():
    resource.setrlimit(resource.RLIMIT_CORE, (resource.RLIM_INFINITY, resource.RLIM_INFINITY))


def task_runner():
    while True:
        try:
            prefix = task_q.get(block=False)
        except:
            return

        with open(log_path.joinpath(f'{prefix}-stdout.log'), 'w') as stdoutfile, \
            open(log_path.joinpath(f'{prefix}-stderr.log'), 'w') as stderrfile, \
            subprocess.Popen(test_cmd, cwd=luvco_project_path, stdout=stdoutfile, stderr=stderrfile, preexec_fn=unlimited_coredump) as p:

            retcode = p.wait(timeout=None)
            if retcode == 0:
                print (f'{prefix} pid={p.pid}, run end')
            else:
                print (f'{prefix} pid={p.pid}, run ERROR!!!!!')
                r = re.compile(f'core-.*-{p.pid}')
                corefile = [f for f in os.listdir(luvco_project_path) if re.match(r, f)]
                if len(corefile) != 1:
                    print (f"ERROR: core files error, {corefile}")
                else:
                    corefile = corefile[0]
                    os.rename(luvco_project_path.joinpath(corefile), log_path.joinpath(f'{prefix}-coredump'))

        task_q.task_done()


def main():
    parser = argparse.ArgumentParser(prog="run test", description="run test multi times and save log")
    parser.add_argument("program")
    parser.add_argument("-times", '-t', default=4, type=int)
    parser.add_argument("-parallel", '-p', default=2, type=int)
    args = parser.parse_args()

    program: str = args.program
    times : int = args.times
    parallel : int = args.parallel

    global test_cmd
    test_cmd = program.split(' ')

    timeprefix = datetime.datetime.now().strftime('%d%H%M%S')
    program_name = Path(test_cmd[0]).name
    prefix = f'{timeprefix}-{program_name}'

    print (f'================================')
    print (f'program : {program}')
    print (f'test-cmd : {test_cmd}')
    print (f'times : {times}')
    print (f'parallel : {parallel}')
    print (f'output prefix : {prefix}')
    print (f'================================')

    for i in range(times):
        task_q.put(f'{prefix}-{i}')

    for i in range(parallel):
        t = threading.Thread(target=task_runner)
        t.start()

    task_q.join()
    print("=========== all test over ============")


if __name__ == '__main__':
    main()