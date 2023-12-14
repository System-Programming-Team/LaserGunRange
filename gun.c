#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wiringPiI2C.h>
#include <wiringPi.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>

// Define some device parameters
#define I2C_ADDR   0x27 // I2C device address

// Define some device constants
#define LCD_CHR  1 // Mode - Sending data
#define LCD_CMD  0 // Mode - Sending command

#define LINE1  0x80 // 1st line
#define LINE2  0xC0 // 2nd line

#define LCD_BACKLIGHT   0x08  // On
// LCD_BACKLIGHT = 0x00  # Off

#define ENABLE  0b00000100 // Enable bit

#define BUFFER_MAX 3
#define DIRECTION_MAX 256
#define VALUE_MAX 256

#define IN 0
#define OUT 1

#define LOW 0
#define HIGH 1

#define PIN 20
#define POUT 21

#define LOUT 26

#define LEDOUT 17
#define BUF_SIZE  30

void lcd_init(void);
void lcd_byte(int bits, int mode);
void lcd_toggle_enable(int bits);
void lcd_off(void);

void typeInt(int i);
void typeFloat(float myFloat);
void lcdLoc(int line); //move cursor
void ClrLcd(void); // clr LCD return home
void typeln(const char *s);
void typeChar(char val);
int ffd;  // seen by all subroutines
int out=0;
int conl=0;
void error_handling(char *message) {
  fputs(message, stderr);
  fputc('\n', stderr);
  exit(1);
}

static int GPIOExport(int pin) {
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

  char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
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

static int GPIORead(int pin) {
  char path[VALUE_MAX];
  char value_str[3];
  int fd;

  snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_RDONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio value for reading!\n");
    return (-1);
  }

  if (-1 == read(fd, value_str, 3)) {
    fprintf(stderr, "Failed to read value!\n");
    return (-1);
  }

  close(fd);

  return (atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
  static const char s_values_str[] = "01";
  char path[VALUE_MAX];
  int fd;

  snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_WRONLY);
  if (-1 == fd) {
    fprintf(stderr, "Failed to open gpio value for writing!\n");
    close(fd);
    return (-1);
  }

  if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
    fprintf(stderr, "Failed to write value!\n");
    close(fd);
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

void *receiver(void* data) {
  int arr[5];
  int sock = *(int*) data;
  arr[0]=1;
  while(1) {
    
    if(recv(sock, arr, sizeof(arr), 0) <=0 ) 
    {
      printf("END\n");
      break;
    }
    else if(arr[0] == 1) {
      ClrLcd();
      if(arr[2] < 0) {
        conl=0;
        if(GPIOWrite(LEDOUT, 0) == -1) exit(1);
        lcdLoc(LINE1);
        typeln("Score:");
        typeInt(arr[1]);
        lcdLoc(LINE2);
        typeln("Time:");
        typeInt(0);
        typeln("/ST:");
        typeInt(arr[3]);
        typeln("/");
        typeInt(3 + arr[4]);
      }
      else {
        if(conl == 0) {
          conl = 1;
          if(GPIOWrite(LEDOUT, 1) == -1) exit(1);
        }
        lcdLoc(LINE1);
        typeln("Score:");
        typeInt(arr[1]);
        lcdLoc(LINE2);
        typeln("Time:");
        typeInt(arr[2]);
        typeln("/ST:");
        typeInt(arr[3]);
        typeln("/");
        typeInt(3 + arr[4]);
      }  
    }
    else{
      out = 1;
      break;
    }
  }
}

int main(int argc, char *argv[])
{
  int sock;
  pid_t pid;
  int arr[5];
  struct sockaddr_in serv_adr;
  pthread_t p_thread;
  int thr_id;
  int bullet = 50;
  int status = 1;
  int prev_status;
  int st;
  if(argc!=3) {
    printf("Usage : %s <IP> <port>\n", argv[0]);
    exit(1);
  }

  if (wiringPiSetup () == -1) exit (1);
  ffd = wiringPiI2CSetup(I2C_ADDR);
  lcd_init(); // setup LC
  
  sock=socket(PF_INET, SOCK_STREAM, 0);  
  memset(&serv_adr, 0, sizeof(serv_adr));
  serv_adr.sin_family=AF_INET;
  serv_adr.sin_addr.s_addr=inet_addr(argv[1]);
  serv_adr.sin_port=htons(atoi(argv[2]));

  if(connect(sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr))==-1)
	  error_handling("connect() error!");

  thr_id = pthread_create(&p_thread, NULL, receiver, (void*)&sock);
  if (thr_id < 0) {
    perror("thread create error : ");
    exit(0);
  }
  if (GPIOExport(POUT) == -1 || GPIOExport(PIN) == -1|| GPIOExport(LOUT) == -1 || GPIOExport(LEDOUT) == -1) {
  return 1;
  }

  if (GPIODirection(POUT, OUT) == -1 || GPIODirection(PIN, IN) == -1 || GPIODirection(LOUT, OUT) == -1 || GPIODirection(LEDOUT, OUT) == -1) {
    return 2;
  }

  if (GPIOWrite(LEDOUT, 1) == -1) return 5;
  do {
    if(out == 1) {
          printf("END\n");
          break;
    }
    if (GPIOWrite(POUT, 1) == -1) {
      return 3;
    }
    //if (GPIOWrite(LOUT, 0) == -1) return 4;
    //GPIOWrite(LOUT, 1);
    prev_status = status;
    status = GPIORead(PIN);
    //printf("GPIORead: %d from pin %d\n", status, PIN);
    if(prev_status == 1 && status == 0 && conl == 1) {
        
        if (GPIOWrite(LOUT, 1) == -1 || GPIOWrite(LEDOUT, 0) == -1) return 4;
        usleep(200000);
        if (GPIOWrite(LOUT, 0) == -1) return 4;

        usleep(1000*500);
        if(GPIOWrite(LEDOUT, 1) == -1) return 4;
    }
    usleep(1000);//1000 * 1000
    
  } while (1);
  pthread_join(p_thread, (void**)&st);
  ClrLcd();
  lcdLoc(LINE1);
  typeln("***GAME OVER***");
  
  if (GPIOWrite(LOUT, 1) == -1 || GPIOWrite(LEDOUT, 0)) return 4;
  usleep(10000);
  if (GPIOWrite(LOUT, 0) == -1) return 4;
  sleep(100);

  ClrLcd();
  if (GPIOUnexport(POUT) == -1 || GPIOUnexport(PIN) == -1 || GPIOUnexport(LOUT) == -1 || GPIOUnexport(LEDOUT)) {
    return 4;
  }
  
  close(sock);

  return 0;
}

// float to string
void typeFloat(float myFloat)   {
  char buffer[20];
  sprintf(buffer, "%4.2f",  myFloat);
  typeln(buffer);
}

// int to string
void typeInt(int i)   {
  char array1[20];
  sprintf(array1, "%d",  i);
  typeln(array1);
}

// clr lcd go home loc 0x80
void ClrLcd(void)   {
  lcd_byte(0x01, LCD_CMD);
  lcd_byte(0x02, LCD_CMD);
}

// go to location on LCD
void lcdLoc(int line)   {
  lcd_byte(line, LCD_CMD);
}

// out char to LCD at current position
void typeChar(char val)   {

  lcd_byte(val, LCD_CHR);
}


// this allows use of any size string
void typeln(const char *s)   {

  while ( *s ) lcd_byte(*(s++), LCD_CHR);

}

void lcd_byte(int bits, int mode)   {

  //Send byte to data pins
  // bits = the data
  // mode = 1 for data, 0 for command
  int bits_high;
  int bits_low;
  // uses the two half byte writes to LCD
  bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT ;
  bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT ;

  // High bits
  wiringPiI2CReadReg8(ffd, bits_high);
  lcd_toggle_enable(bits_high);

  // Low bits
  wiringPiI2CReadReg8(ffd, bits_low);
  lcd_toggle_enable(bits_low);
}

void lcd_toggle_enable(int bits)   {
  // Toggle enable pin on LCD display
  delayMicroseconds(500);
  wiringPiI2CReadReg8(ffd, (bits | ENABLE));
  delayMicroseconds(500);
  wiringPiI2CReadReg8(ffd, (bits & ~ENABLE));
  delayMicroseconds(500);
}


void lcd_init()   {
  // Initialise display
  lcd_byte(0x33, LCD_CMD); // Initialise
  lcd_byte(0x32, LCD_CMD); // Initialise
  lcd_byte(0x06, LCD_CMD); // Cursor move direction
  lcd_byte(0x0C, LCD_CMD); // 0x0F On, Blink Off
  lcd_byte(0x28, LCD_CMD); // Data length, number of lines, font size
  lcd_byte(0x01, LCD_CMD); // Clear display
  delayMicroseconds(500);
}

