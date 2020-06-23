/*
 ============================================================================
 Name        : rdECOMd.c
 Author      : Greg
 Version     :
 Copyright   : (c) 2020 NTI BOilers Inc.
 Description : daemon reads CO value from ECOM combustion analyzer
 ============================================================================
 */

/*
 *  From stackoverflow:
 * daemonize.c
 * This example daemonizes a process, writes a few log messages,
 * sleeps 20 seconds and terminates afterwards.
 * This is an answer to the stackoverflow question:
 * https://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux/17955149#17955149
 * Fork this code: https://github.com/pasce/daemon-skeleton-linux-c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <wiringPi.h>
#include <softPwm.h>

#define PWM0       12                    // this is physical pin 12
#define PWM1       33                    // only on the RPi B+/A+/2

static void skeleton_daemon()
{
    pid_t pid;

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Success: Let the parent terminate */
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    /* Close all open file descriptors */
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }

    /* Open the log file */
    openlog ("rdECOMd", LOG_PID, LOG_DAEMON);
}

int main()
{
	int duty_cycle_00 = 0;
	int duty_cycle_01 = 0;

    skeleton_daemon();

    syslog (LOG_NOTICE, "rdECOMd started.");

    wiringPiSetupPhys();                  // use the physical pin numbers
    pinMode(PWM0, PWM_OUTPUT);            // use the RPi PWM output
	pinMode(PWM1, PWM_OUTPUT);            // only on recent RPis

// Setting PWM frequency to be 10kHz with a full range of 128 steps
// PWM frequency = 19.2 MHz / (divisor * range)
// 10000 = 19200000 / (divisor * 128) => divisor = 15.0 = 15
	pwmSetMode(PWM_MODE_MS);              // use a fixed frequency
	pwmSetRange(128);                     // range is 0-128
	pwmSetClock(15);                      // gives a precise 10kHz signal
	syslog (LOG_NOTICE, "The PWM Output is enabled.");

	while (1)
    {
        //TODO: Insert daemon code here.
		pwmWrite(PWM0, duty_cycle_00);                   // duty cycle of 25% (32/128)
		pwmWrite(PWM1, duty_cycle_01);                   // duty cycle of 50% (64/128)
		usleep(1000000L);
    	duty_cycle_00 = duty_cycle_00 < 100 ? ++duty_cycle_00 : 0;
    	duty_cycle_01 = duty_cycle_01 < 100 ? ++duty_cycle_01 : 0;
    }

/*    syslog (LOG_NOTICE, "rdECOMd terminated."); */
    closelog();

    return EXIT_SUCCESS;
}
