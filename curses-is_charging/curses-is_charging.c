#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <stdint.h> // unsure
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
//#include <time.h> // unsure
//#include <libgen.h> // unsure
#include <errno.h>
#include <signal.h>

#include <ncurses.h>

#include <wiringPi.h>

#include "rc_header.h"

#define BATTERY_CAPACITY 1200 // mAh

unsigned long lastM = 0;
long lastTime;
unsigned long TimeToEmpty;
int Current[10], AvgCurrent;
_Bool charging;

int i2c_file;

void TTE();

_Bool checkRoot()
{
    uid_t uid = getuid(), euid = geteuid();
    return (uid != 0 || uid != euid);
}

static void die(int sig)
{
    close(i2c_file);
    endwin();
    if (sig != 0 && sig != 2) (void)fprintf(stderr, "caught signal %s\n", strsignal(sig));
    if (sig == 2) (void)fprintf(stderr, "Exiting due to Ctrl + C (%s)\n", strsignal(sig));
    exit(0);
}

int main(int argc, char **argv)
{
    int key;
    int i;
    _Bool stopMe = FALSE;
    unsigned long cm;
    unsigned long lm = 0;

    lastTime = millis();
    // note: we're assuming BSD-style reliable signals here
    (void)signal(SIGINT, die);
    (void)signal(SIGHUP, die);
    (void)signal(SIGTERM, die);
    (void)signal(SIGABRT, die);

    if (checkRoot())
    {
        printf("invalid credentials\n");
        printf("sudo %s\n", argv[0]);
        return 1;
    }
    if ((i2c_file = open(devName, O_RDWR)) < 0)
    {
        fprintf(stderr, "[%d] [%s] [%s] I2C: Failed to access %s\n", __LINE__, __FILE__, __func__, devName);
        exit(1);
    }
    if (ioctl(i2c_file, I2C_SLAVE, ADDRESS_ES) < 0)
    {
        fprintf(stderr, "[%d] [%s] [%s] I2C: Failed to acquire bus access/talk to slave 0x%x\n", __LINE__, __FILE__, __func__, ADDRESS_ES);
        exit(1);
    }
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, true);
    curs_set(0);
    move(0, 1);
    printw("I2C: Acquiring bus to 0x%x", ADDRESS_ES);
    TTE();
    while (stopMe == FALSE)
    {
        if (millis() > lastTime + 100)
            TTE();
        cm = millis();
        if (cm - lm >= 1000)
        {
            lm = cm;
            move(3, 2);
            if (!charging)
            {
                printw("Time To Empty: %ld hr %ld min", TimeToEmpty / 60, TimeToEmpty % 60);
            }
            else
            {
                for (i = 0; i < 30; i++)
                    printw(" ");
                move(3, 2);
                printw("Charging!");
            }
            refresh();
        }
        key = getch();
        if (key > -1 && key == 'q')
            stopMe = TRUE;
    }
    endwin();
    close(i2c_file);
    return 0;
} // end main

void TTE()
{
    unsigned char cmd[16];
    int i;
    char HB = 0, LB = 0;
    int Iraw = 0;
    int p = 0;
    float perc = 0;

    for (i = 9; i > 0; i--)
        Current[i] = Current[i - 1];
    cmd[0] = 0x0E;
    if (write(i2c_file, cmd, 1) == 1)
    {
        usleep(10000); // wait for messages to be sent
        char buf[1];
        if (read(i2c_file, buf, 1) == 1)
            HB = buf[0];
        if (read(i2c_file, buf, 1) == 1)
            LB = buf[0];
        Iraw = (long) (((HB << 8) + LB) >> 4) * 5 / 4;
        Current[0] = Iraw;
        for (i = 9; i >= 0; i--)
        {
            if (Current[i] < 0)
            {
                Current[i] = -Current[i];
                charging = FALSE;
            }
            else
                charging = TRUE;
        }
        cmd[0] = 0x02;
        if (write(i2c_file, cmd, 1) == 1)
        {
            usleep(10000); // wait for messages to be sent
            char buf[1];
            if (read(i2c_file, buf, 1) == 1)
                p = buf[0];
            perc = (float)p / 2;
        }
        //usleep(10000); // wait for messages to be sent
        // Check that energyShield is not charging
        for (i = 0; i < 10; i++)
            AvgCurrent += Current[i];
        AvgCurrent /= 10;
        TimeToEmpty = (unsigned long) BATTERY_CAPACITY * perc * 60 / AvgCurrent / 200;
    }
}
