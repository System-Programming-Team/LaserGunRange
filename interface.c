#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define NUM_LED 5       //use 5 LEDs
#define LED 20          //LED will use GPIO20~GPIO24 serially
#define PWM 0           //buzzer will use GPIO18(PWM0)

#define SOUND_VOLUME 1000

#define IN 0
#define OUT 1
#define LOW 0
#define HIGH 1

#define VALUE_MAX 40
#define DIRECTION_MAX 128

#define HZ 1000000000               //1HZ
#define SCORE_TO_BONUS_STAGE1 1000
#define SCORE_TO_BONUS_STAGE2 3000
#define MIN 60

uint8_t led;       //use rightmost 5 bit to describe LED's on/off
uint8_t newled;
uint8_t hitnote;
int remain_time = 0;
int shot_time;
bool sound = false;
int continue_game = 1;
int stage;
int bonus_stage;
int cur_score;
double note[] = {261.63, 293.66, 329.63, 349.23, 392.00, 440.00, 493.88, 523.26};

void stageStartUpRoutine(void);
void endRoutine(void);
void *openCV(void *arg);
void *laserGun(void *arg);
void *buzzer(void *arg);
void ledSet(void);
void *portal(void* arg);
static int GPIOExport(int pin);
static int GPIODirection(int pin, int dir);
static int GPIOWrite(int pin, int value);
static int GPIOUnexport(int pin);
static int PWMExport(int pwmnum);
static int PWMEnable(int pwmnum);
static int PWMWritePeriod(int pwmnum, int value);
static int PWMWriteDutyCycle(int pwmnum, int value);
void error_handling(char *message);

int main(int argc, char *argv[]) {
    int serv_sock, clnt_sock = -1;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    int status = 0;
    pthread_t p_thread[3];

    if (argc != 2) {
        printf("Usage : %s <port>\n", argv[0]);
    }

    for (int i = 0; i < 5; i++) {
        if (GPIOExport(LED + i) == -1){
            fprintf(stderr, "%d\n", LED + i);
            error_handling("gpio export error");
        }
        usleep(50 * 1000);
        if (GPIODirection(LED + i, OUT) == -1){
            fprintf(stderr, "%d\n", LED + i);
            error_handling("gpio direction error");
        }
    }

    if (PWMExport(PWM) == -1) 
        error_handling("pwm export error");
    
    PWMWritePeriod(PWM, HZ / note[0]);
    PWMWriteDutyCycle(PWM, SOUND_VOLUME);
    PWMEnable(PWM);
    PWMWriteDutyCycle(PWM, LOW);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) error_handling("socket() error");
    printf("[*] socket initialized\n");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("bind() error");

    if (listen(serv_sock, 2) == -1) error_handling("listen() error");

    for (int i = 0; i < 2; i++) {
        printf("[*] wait for client\n");
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);
        if (clnt_sock == -1) {
            error_handling("accept error");
        }

        printf("[*] client join, fd = %d, ip = %d.%d.%d.%d, port = %d\n", 
        clnt_sock, clnt_addr.sin_addr.s_addr & 255, (clnt_addr.sin_addr.s_addr >> 8) & 255, (clnt_addr.sin_addr.s_addr >> 16) & 255, 
        (clnt_addr.sin_addr.s_addr >> 24) & 255, clnt_addr.sin_port);

        if (i == 0) {
            pthread_create(&p_thread[0], NULL, openCV, (void *)&clnt_sock);
        }
        else pthread_create(&p_thread[1], NULL, laserGun, (void *)&clnt_sock);
    }
    pthread_create(&p_thread[2], NULL, buzzer, NULL);

    //main thread
    const int max_stage = 3;
    for (stage = 1; stage <= max_stage + bonus_stage; stage++) {
        stageStartUpRoutine();
        remain_time = MIN;
        while (remain_time >= 0) {
            //stage start
            int temp_time = remain_time;
            int temp_score = cur_score;
            int sec = 8 - stage;
            if(sec < 5) sec = 5;
            ledSet();
            while(remain_time >= 0 && temp_time - remain_time <= sec) {
                //set start
                for (int i = 0; i < NUM_LED; i++)
                    if (GPIOWrite(LED + i, 0x1 & (led >> i)) == -1)
                        error_handling("gpio write error");

                if (led == 0) {
                    //bonus score when shot all target in a set
                    cur_score += (cur_score - temp_score) * (8 - sec);
                    break;
                }
            }
            if (cur_score >= SCORE_TO_BONUS_STAGE1 && bonus_stage == 0) bonus_stage++;
            if (cur_score >= SCORE_TO_BONUS_STAGE2 && bonus_stage == 1) bonus_stage++;
        }
    }

    endRoutine();

    continue_game = 0;

    pthread_join(p_thread[1], (void **)&status);
    sound = 1;
    pthread_join(p_thread[2], (void **)&status);

    for (int i = 0; i < NUM_LED; i++)
        if (GPIOUnexport(LED + i) == -1)
            error_handling("gpio unexport error");

    close(clnt_sock);
    close(serv_sock);

    return (0);
}

void stageStartUpRoutine(void) {
    /*
    effect to start stage
    on all LED first.
    and off one LED every second with buzzer sound
    */
    for (int i = 0; i < NUM_LED; i++)
        if (GPIOWrite(LED + i, HIGH) == -1)
            error_handling("gpio write error");
    sleep(2);
    PWMWritePeriod(PWM, HZ / note[4]);
    for (int i = NUM_LED - 1; i > 0; i--) {
        if (GPIOWrite(LED + i, 0) == -1)
            error_handling("gpio write error");
        PWMWriteDutyCycle(PWM, SOUND_VOLUME);
        usleep(700 * 1000);
        PWMWriteDutyCycle(PWM, LOW);
        usleep(300 * 1000);
    }
    if (GPIOWrite(LED, LOW) == -1)
        error_handling("gpio write error");
    PWMWritePeriod(PWM, HZ / note[7]);
    PWMWriteDutyCycle(PWM, SOUND_VOLUME);
    sleep(1);
    PWMWriteDutyCycle(PWM, LOW);
}

void endRoutine(void) {
    for (int i = 0; i < NUM_LED; i++)
        if (GPIOWrite(LED + i, LOW) == -1)
            error_handling("gpio write error");
}

void *openCV(void *arg) {
    /*
    communicate with camera
    */
    int clnt = *(int *)arg;
    printf("camera on\n");
    while(continue_game) {
        if (recv(clnt, &newled, sizeof(newled), 0) <= 0) break;
        printf("%d\n", newled);
        if(newled & led) {
            for (hitnote = 0; newled >> hitnote; hitnote++);
            hitnote--;
            sound = true;
            cur_score += newled + stage;
            led = (~newled) & led;
        }
    }
    continue_game = 0;
    endRoutine();
}

void *laserGun(void *arg) {
    /*
    commuication to lasergun
    */
    int clnt = *(int *)arg;
    printf("lasergun on\n");
    int arr[5];
    while (continue_game) {
        arr[0] = continue_game;
        arr[1] = cur_score;
        arr[2] = remain_time--;
        arr[3] = stage;
        arr[4] = bonus_stage;
        send(clnt, arr, sizeof(arr), 0);
        sleep(1);
    }
}

void *buzzer(void *arg) {
    while(1) {
        while(!sound) {};
        if (continue_game == 0) break;
        sound = false;
        PWMWritePeriod(PWM, HZ / note[hitnote]);
        PWMWriteDutyCycle(PWM, SOUND_VOLUME);
        usleep(500 * 1000);
        PWMWriteDutyCycle(PWM, LOW);
    }
}

void ledSet(void) {
    /*
    set which led on
    */
    srand(time(NULL));
    led = 0;
    do {
        led = rand() % 32;
    } while(led == 0);
    newled = led;
}

static int GPIOExport(int pin) {
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir) {
    static const char s_directions_str[] = "in\0out";

    char path[DIRECTION_MAX];
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }

    if (-1 ==
        write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int GPIOWrite(int pin, int value) {
    static const char s_values_str[] = "01";

    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
        fprintf(stderr, "Failed to write value!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int GPIOUnexport(int pin) {
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int PWMExport(int pwmnum) {
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    int fd, byte;

    // TODO: Enter the export path.
    fd = open("/sys/class/pwm/pwmchip0/export", O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open export for export!\n");
        return (-1);
    }

    byte = snprintf(buffer, BUFFER_MAX, "%d", pwmnum);
    write(fd, buffer, byte);
    close(fd);

    sleep(1);

    return (0);
}

static int PWMEnable(int pwmnum) {
    static const char s_enable_str[] = "1";

    char path[DIRECTION_MAX];
    int fd;

    // TODO: Enter the enable path.
    snprintf(path, DIRECTION_MAX, "/sys/class/pwm/pwmchip0/pwm%d/enable", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open in enable!\n");
        return -1;
    }

    write(fd, s_enable_str, strlen(s_enable_str));
    close(fd);

    return (0);
}

static int PWMWritePeriod(int pwmnum, int value) {
    char s_value_str[VALUE_MAX];
    char path[VALUE_MAX];
    int fd, byte;

    // TODO: Enter the period path.
    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/period", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open in period!\n");
        return (-1);
    }
    byte = snprintf(s_value_str, VALUE_MAX, "%d", value);

    if (-1 == write(fd, s_value_str, byte)) {
        fprintf(stderr, "Failed to write value in period!\n");
        close(fd);
        return -1;
    }
    close(fd);

    return (0);
}

static int PWMWriteDutyCycle(int pwmnum, int value) {
    char s_value_str[VALUE_MAX];
    char path[VALUE_MAX];
    int fd, byte;

    // TODO: Enter the duty_cycle path.
    snprintf(path, VALUE_MAX, "/sys/class/pwm/pwmchip0/pwm%d/duty_cycle", pwmnum);
    fd = open(path, O_WRONLY);
    if (-1 == fd) {
        fprintf(stderr, "Failed to open in duty cycle!\n");
        return (-1);
    }
    byte = snprintf(s_value_str, VALUE_MAX, "%d", value);

    if (-1 == write(fd, s_value_str, byte)) {
        fprintf(stderr, "Failed to write value in duty cycle!\n");
        close(fd);
        return -1;
    }
    close(fd);

    return (0);
}

void error_handling(char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
