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

#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <wiringPi.h>
#include <softPwm.h>
#include <sys/reboot.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>
#include <error.h>
#include <errno.h>
#include <string.h>


#define BAUDRATE B38400
#define SERIALDEVICE "/dev/ttyUSB0"
#define _POSIX_SOURCE 1 /* POSIX compliant source */

#define PWM0       12                    // this is physical pin 12
#define PWM1       33                    // only on the RPi B+/A+/2

typedef struct _ECOM_Response ECOM_CO_response;

struct _ECOM_Response {
	char leader;
	char len_hi_byte;
	char len_low_byte;
	char command_hi_byte;
	char command_lo_byte;
	char delim_00;
	char request_hi_byte;
	char request_lo_byte;
	char data_04;
	char data_03;
	char data_02;
	char data_01;
	char delim_01;
	char checksum_hi_byte;
	char checksum_lo_byte;
	uint8_t carriage_return;
};

const uint8_t ECOM_request[] = {'$', '0', 'A', '0', '5', '0', '4', '5', 'E', 0x0D }; /* Read CO value */
const long MAX_SERIAL_TIMEOUTS = 10L;
const long MAX_CONNECT_FAIL = 10L;
const long MAX_CHECKSUM_ERRORS = 10L;
const int PWM_RANGE = 1024;

int input_sz;
int i_r;
int serialfd;
int c;
int i_e;
struct termios oldtio;
struct termios newtio;
struct timeval tv_now;
struct timeval tv_start;
long useconds_now;
long useconds_start;
uint8_t buf[255];
struct timeval timeout;
fd_set read_fds;
fd_set write_fds;
fd_set except_fds;
long write_timeout_counter = 0L;
long read_timeout_counter = 0L;
long not_connected_counter = 0L;
long checksum_error_counter = 0L;
void * p_v;

union ECOM_Data {
	ECOM_CO_response ecom_response;
	uint8_t ecom_buf[255];
};

union ECOM_Data ecom_data;

/* Define state values */
enum State_Values {
	Entry_State = 0,
	Not_Connected_00 = 1,
	Not_Connected_01 = 2,
	Writing_Reading_State = 3,
	Update_PWM_State = 4,
	System_Reset_State = 5
};

typedef enum State_Values(*func_ptr)(int * serialfd);

typedef struct state_descriptor {
	char const *state_name;
	enum State_Values const sv;
	func_ptr fp;
} STATE_DESCRIPTOR,*LPSTATE_DESCRIPTOR;

enum State_Values Entry_state_fn(int * serialfd);
enum State_Values Not_Connected_00_state_fn(int * serialfd);
enum State_Values Not_Connected_01_state_fn(int * serialfd);
enum State_Values Writing_Reading_State_state_fn(int * serialfd);
enum State_Values Update_PWM_State_state_fn(int * serialfd);
enum State_Values System_Reset_State_state_fn(int * serialfd);

func_ptr lookup_state_fn(enum State_Values sv);

STATE_DESCRIPTOR const sd_descriptors[] = {
	{"Entry_State",Entry_State,Entry_state_fn},
	{"Not_Connected_00",Not_Connected_00,Not_Connected_00_state_fn},
	{"Not_Connected_01",Not_Connected_01,Not_Connected_01_state_fn},
	{"Writing_Reading_State",Writing_Reading_State,Writing_Reading_State_state_fn},
	{"Update_PWM_State",Update_PWM_State,Update_PWM_State_state_fn},
	{"System_Reset_State",System_Reset_State,System_Reset_State_state_fn},
};


bool is_checksum_ok(union ECOM_Data * ed,int count);


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
	int i_r;

	func_ptr sf;
	enum State_Values sc;

	skeleton_daemon();

    syslog (LOG_NOTICE, "rdECOMd started.");

	sc = Entry_State;
    while (1)
    {
        //TODO: Insert daemon code here.
		sf = lookup_state_fn(sc);
		if (NULL != sf)
		{
			sc = sf(&serialfd);
		}
		else
		{
			syslog (LOG_INFO,"Error: Undetermined state function %d, rebooting.", sc);
			sync();
			i_r = reboot(RB_AUTOBOOT);
		}
    }

/*    syslog (LOG_NOTICE, "rdECOMd terminated."); */
    closelog();

    return EXIT_SUCCESS;
}

enum State_Values Entry_state_fn(int * serialfd)
{
	wiringPiSetupPhys();                  // use the physical pin numbers
	pinMode(PWM0, PWM_OUTPUT);            // use the RPi PWM output
	pinMode(PWM1, PWM_OUTPUT);            // only on recent RPis

// Setting PWM frequency to be 10kHz with a full range of 128 steps
// PWM frequency = 19.2 MHz / (divisor * range)
// 10000 = 19200000 / (divisor * 128) => divisor = 15.0 = 15
	pwmSetMode(PWM_MODE_MS);              // use a fixed frequency
	pwmSetRange(PWM_RANGE);                     // set PWM range
	pwmSetClock(15);                      // gives a precise 10kHz signal
	syslog (LOG_NOTICE, "The PWM Output is enabled.");
	return Not_Connected_00;
}

enum State_Values Not_Connected_00_state_fn(int * serialfd)
{

	/* open the device to be non-blocking (read will return immediatly) */
	*serialfd = open(SERIALDEVICE, O_RDWR | O_NOCTTY | O_NDELAY);
	i_r = fcntl(*serialfd, F_SETFL, 0);
	i_e = errno;
	if (*serialfd < 0)
	{
		syslog(LOG_INFO, "%s: %s",SERIALDEVICE,strerror(i_e));
		usleep(10000000L);
		if (MAX_CONNECT_FAIL < ++not_connected_counter)
		{
			return Not_Connected_01;
		}
		return Not_Connected_00;
	}

	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_SET(*serialfd, &read_fds);
	FD_SET(*serialfd, &write_fds);

	tcgetattr(*serialfd,&oldtio); /* save current port settings */
	/* set new port settings for non-canonical input processing */
	newtio = oldtio;
	cfmakeraw(&newtio);
	i_r = cfsetispeed(&newtio, BAUDRATE);
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;

	tcflush(*serialfd, TCIFLUSH);
	tcsetattr(*serialfd,TCSANOW,&newtio);
	syslog(LOG_NOTICE, "%s opened.",SERIALDEVICE);
	return Writing_Reading_State;
}

enum State_Values Not_Connected_01_state_fn(int * serialfd)
{

	/* open the device to be non-blocking (read will return immediatly) */
	*serialfd = open(SERIALDEVICE, O_RDWR | O_NOCTTY | O_NDELAY);
	i_r = fcntl(*serialfd, F_SETFL, 0);
	i_e = errno;
	if (*serialfd < 0)
	{
		syslog(LOG_INFO, "%s: %s",SERIALDEVICE,strerror(i_e));
		++not_connected_counter;
		usleep(60000000L);
		return Not_Connected_01;
	}

	FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);
	FD_SET(*serialfd, &read_fds);
	FD_SET(*serialfd, &write_fds);

	tcgetattr(*serialfd,&oldtio); /* save current port settings */
	/* set new port settings for non-canonical input processing */
	newtio = oldtio;
	cfmakeraw(&newtio);
	i_r = cfsetispeed(&newtio, BAUDRATE);
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;

	tcflush(*serialfd, TCIFLUSH);
	tcsetattr(*serialfd,TCSANOW,&newtio);
	syslog(LOG_NOTICE, "%s opened.",SERIALDEVICE);
	return Writing_Reading_State;
}

enum State_Values System_Reset_State_state_fn(int * serialfd)
{
	syslog (LOG_INFO,"System reset, rebooting.");
	sync();
	i_r = reboot(RB_AUTOBOOT);
	return System_Reset_State;
}

enum State_Values Writing_Reading_State_state_fn(int * serialfd)
{
	struct timeval tv_start;
	int input_sz;
	int output_sz;
	long start_time;
	long now_time;
	struct timeval tv_now;
	struct timeval rd_timeout;
	struct timeval wr_timeout;
	uint8_t crcc;

	bool b_r;
	bool b_timed_out = TRUE;
	long dw_rc;
	long dw_nbr;
	long dw_nbw;
	int i_i;
	int i_r;
	int i_rs;
	int i_ws;
	int i_len;
	void * p_v;

	i_r = usleep(3000000L);
	FD_SET(*serialfd, &read_fds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000000L;
	p_v = memset(buf,0,sizeof(buf));
	output_sz = input_sz = 0;
	if ((i_r = select((*serialfd) + 1, NULL,&write_fds, NULL, &timeout)) == 1)
	{
		// fd is ready for reading
		output_sz = write(*serialfd,ECOM_request,sizeof(ECOM_request));
		i_r = gettimeofday(&tv_now,NULL);
		useconds_now = (tv_now.tv_sec*1000000L)+tv_now.tv_usec;
		b_timed_out = FALSE;
		write_timeout_counter = 0;
	}
	else
	{
		// timeout or error
		i_r = gettimeofday(&tv_now,NULL);
		b_timed_out = TRUE;
		if (MAX_SERIAL_TIMEOUTS < ++write_timeout_counter)
		{
			return System_Reset_State;
		}
		return Writing_Reading_State;
	}
	FD_SET(*serialfd, &write_fds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000000L;
	if ((i_r = select((*serialfd) + 1, &read_fds,NULL, NULL, &timeout)) == 1)
	{
		// fd is ready for reading
		input_sz = read(*serialfd,ecom_data.ecom_buf,sizeof(ecom_data.ecom_buf));
		i_r = gettimeofday(&tv_now,NULL);
		useconds_now = (tv_now.tv_sec*1000000L)+tv_now.tv_usec;
		b_timed_out = FALSE;
		read_timeout_counter = 0;
	}
	else
	{
		// timeout or error
		++read_timeout_counter;
		i_r = gettimeofday(&tv_now,NULL);
		b_timed_out = TRUE;
		if (MAX_SERIAL_TIMEOUTS < read_timeout_counter)
		{
			syslog (LOG_INFO,"Maximum read timeouts exceeded, restarting.");
			return System_Reset_State;
		}
		return Writing_Reading_State;
	}
	FD_SET(*serialfd, &read_fds);
	checksum_error_counter = is_checksum_ok(&ecom_data,input_sz) ? 0 : ++checksum_error_counter;
	if (MAX_CHECKSUM_ERRORS < checksum_error_counter)
	{
		syslog (LOG_INFO,"Maximum checksum errors exceeded, restarting.");
		return System_Reset_State;
	}
	if (!is_checksum_ok(&ecom_data,input_sz))
	{
		return Writing_Reading_State;
	}

	return Update_PWM_State;
}

enum State_Values Update_PWM_State_state_fn(int * serialfd)
{
	int i_r;
	uint16_t CO_value = 0;
	char cccv[5];
	i_r = sprintf(cccv, "%c%c%c%c",
		ecom_data.ecom_response.data_04,
		ecom_data.ecom_response.data_03,
		ecom_data.ecom_response.data_02,
		ecom_data.ecom_response.data_01);
	CO_value = (uint16_t)strtol(cccv, NULL, 16);
	pwmWrite(PWM0, CO_value);                   // duty cycle of 25% (32/128)
	pwmWrite(PWM1, CO_value);                   // duty cycle of 50% (64/128)
/*	syslog (LOG_INFO,"CO PWM value: %d",CO_value); */
	return Writing_Reading_State;
}


func_ptr lookup_state_fn(enum State_Values sv)
{
	void * p_v;
	int i_i;
	for (i_i = 0; i_i < sizeof(sd_descriptors) / sizeof(STATE_DESCRIPTOR); i_i++)
	{
		if (sv == sd_descriptors[i_i].sv)
		{
			p_v = (void *)sd_descriptors[i_i].fp;
			return (func_ptr)p_v;
		}
	}
	return NULL;
}


bool is_checksum_ok(union ECOM_Data * ed,int count)
{
	char ccsum[3];
	char scsum[3];
	bool b_r = FALSE;
	uint8_t sum;
	int i_x;
	int i_r;
	for (i_x = 0,sum = 0;(i_x < sizeof(ECOM_CO_response)-3) && (i_x < count);i_x++)
	{
		sum += ed->ecom_buf[i_x];
	}
	i_r = snprintf(ccsum,sizeof(ccsum),"%02X",sum);
	for (i_x = 0;i_x < sizeof(ccsum);i_x++)
	{
		ccsum[i_x] = toupper(ccsum[i_x]);
	}
	i_r = snprintf(scsum,sizeof(scsum),"%c%c",ed->ecom_response.checksum_hi_byte,ed->ecom_response.checksum_lo_byte);
	for (i_x = 0;i_x < sizeof(scsum);i_x++)
	{
		scsum[i_x] = toupper(scsum[i_x]);
	}
	return !strncmp(ccsum,scsum,(size_t)3);
}

