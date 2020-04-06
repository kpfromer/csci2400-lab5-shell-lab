//
// tsh - A tiny shell program with job control
//
// Kyle Pfromer kypf2522
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

// volatile pid_t foreground_process;
// volatile int program_count = 0;
// volatile std::set<pid_t> program_processes;
// volatile std::set<pid_t> jobs_processes;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine
//
int main(int argc, char *argv[])
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF)
  {
    switch (c)
    {
    case 'h': // print help message
      usage();
      break;
    case 'v': // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':          // don't print a prompt
      emit_prompt = 0; // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT, sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler); // ctrl-z
  Signal(SIGCHLD, sigchld_handler); // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler);

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for (;;)
  {
    //
    // Read command line
    //
    if (emit_prompt)
    {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
    {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin))
    {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); //control never reaches here
}

void print_job(pid_t pid, char *command)
{
  printf("[%d] (%d) %s", pid2jid(pid), pid, command);
}

/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv);
  if (argv[0] == NULL)
    return; /* ignore empty lines */

  if (!builtin_cmd(argv))
  {
    // temporarily block signal (receiving signals at 9:47)
    sigset_t mask_all, mask_child, prev_mask;
    pid_t pid;
    // todo: signal handling
    sigfillset(&mask_all);
    sigaddset(&mask_child, SIGCHLD);

    // Block SIGCHLD and save previous blocked set
    sigprocmask(SIG_BLOCK, &mask_child, &prev_mask);
    if ((pid = fork()) == 0)
    {
      setpgid(0, 0);
      // Restore SIGCHLD for the child
      sigprocmask(SIG_SETMASK, &prev_mask, NULL);
      execve(argv[0], argv, NULL);
    }
    else
    {
      if (bg)
      {
        // Block ALL signals
        // sigprocmask(SIG_BLOCK, &mask_all, NULL);
        addjob(jobs, pid, BG, cmdline);
        // Restore previous blocked set, unblocking SIGINT, SIGCHLD, SIGTSTP
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        print_job(pid, cmdline);
      }
      else
      {
        // Block ALL signals for the parent
        // sigprocmask(SIG_BLOCK, &mask_all, NULL);
        // add jobs to jobs list
        addjob(jobs, pid, FG, cmdline);
        // Restore previous blocked set, unblocking ALL signals for the parent
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        // wait for the job to finish
        waitfg(pid);
      }
    }
  }
  return;
}

// https://stackoverflow.com/questions/20032154/the-difference-between-execv-and-execve
// reap all terminated children: put wait in a loop (receiving signals minute: 19:41)
// temp block signal receiving signals at 11:14

/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
  string cmd(argv[0]);
  if (strcmp(argv[0], "quit") == 0)
    exit(0);
  else if (strcmp(argv[0], "fg") == 0)
  {
    do_bgfg(argv);
    return 1;
  }
  else if (strcmp(argv[0], "bg") == 0)
  {
    do_bgfg(argv);
    return 1;
  }
  else if (strcmp(argv[0], "jobs") == 0)
  { // lists background jobs
    listjobs(jobs);
    return 1;
  }
  return 0; /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
  struct job_t *jobp = NULL;

  /* Ignore command if no argument */
  if (argv[1] == NULL)
  {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }

  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0]))
  {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid)))
    {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%')
  {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid)))
    {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }
  else
  {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  string cmd(argv[0]);

  if (cmd == "fg") // can run on suspended processes and background processes
  {
    pid_t pid = jobp->pid;
    if (jobp->state == ST)
    {
      // start up the process
      kill(-pid, SIGCONT);
    }
    jobp->state = FG;
    waitfg(pid);
  }
  else
  { // only runs on suspended processes
    // say running in background
    jobp->state = BG;
    // start up process (group) again with SIGCONT
    pid_t pid = jobp->pid;
    kill(-pid, SIGCONT);
    print_job(pid, jobp->cmdline);
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{

  // catch if the pid is nothing or if it is not the foreground tasks
  if (pid == 0 || pid != fgpid(jobs))
  {
    return;
  }

  // while there is still a foreground task
  while (fgpid(jobs))
  {
    // wait a bit
    sleep(1);
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//

// follow safe signal handler guidelines (receiving signals at 12:51)

/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig)
{
  // pid_t pid;
  // while ((pid = waitpid(-1,)))

  // use waitpid to wait for waiting for specific group?
  // waitpid(-);

  pid_t pid;
  int status;
  struct job_t *job;

  // reap terminated processes and handle stopped processes (WNOHANG = return 0 if they have not be terminated yet)
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
  {
    // reap terminated processes
    if (WIFEXITED(status))
    { // terminated properly via exit / return
      deletejob(jobs, pid);
    }
    else if (WIFSIGNALED(status))
    { // uncaught signal
      job = getjobpid(jobs, pid);
      printf("Job [%d] (%d) terminated by signal %d\n", job->jid, pid, WTERMSIG(status));
      deletejob(jobs, pid);
    }
    // handle stopped processes
    else if (WIFSTOPPED(status))
    {
      job = getjobpid(jobs, pid);
      job->state = ST;
      printf("Job [%d] (%d) stopped by signal %d\n", job->jid, pid, WSTOPSIG(status));
    }
  }

  // check for errors (not no child error)
  if (pid < 0 && errno != ECHILD)
  {
    printf("waitpid error");
    exit(-1);
  }

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig)
{
  // send signal to foreground process using the kill command (misnomer)
  pid_t fpid = fgpid(jobs);
  if (fpid != 0)
    kill(-fpid, SIGINT);

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig)
{
  // send signal to foreground process using the kill command (misnomer)
  // todo: send to foreground_process group
  // kill(-foreground_process, SIGTSTP);

  // send signal to foreground process using the kill command (misnomer)
  pid_t fpid = fgpid(jobs);
  if (fpid != 0)
    kill(-fpid, SIGTSTP);

  return;
}

/*********************
 * End signal handlers
 *********************/
