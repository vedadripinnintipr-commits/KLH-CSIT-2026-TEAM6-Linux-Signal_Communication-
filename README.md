# Linux-Based Process Communication and Control System

## Requirements (Ubuntu)
```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev
```

## Build and run
Open Terminal in this folder:
```bash
make
./process_control
```
Run from this folder because the GUI starts `./worker` using a relative path.

## Features
- Create child processes with `fork()
- Execute the worker program with `exec()`
- Inter-process communication using `pipe()`, `write()` and `read()`
- Pause and resume using `SIGSTOP` and `SIGCONT`
- Request termination using `SIGTERM`
- Collect child exit status using `waitpid()`
- Display PID, PPID, status, CPU time and memory
- Read system CPU and memory information from `/proc`

## Demonstration
1. Click Create Process.
2. Select the worker row.
3. Type a message and click Send Message. The worker prints it in the terminal.
4. Try Pause, Resume and Terminate.
5. Observe the process table and System Log.

CPU Time is cumulative CPU time, not an instantaneous percentage. The table shows this application and its worker processes, not every process on the computer.
