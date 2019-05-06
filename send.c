#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/timeb.h>
#include <signal.h>
#include <math.h>
#include "link_emulator/lib.h"

#define HOST "127.0.0.1"
#define PORT 10000
#define CRC 0x1021
#define SUM_POSITION 1399


struct stat file_info;
int SPEED;
int DELAY;
char* filename;
char check_sum;
int file_size;
int BDP; 
int window;
int ack, next_ack, read_messages;
int buff_messages = 0, done;
/*
un buffer pentru mesajele trimise pentru care
nu am primit inca ack 
*/
msg* buffer;
/*
un vector de flag-uri ca sa verific daca un frame
a priit sau nu ack
*/
int* isAcked;
int timeout;

/*
aceasta functie va calcula check-sum pe cate un octet
pentru a vedea daca un frame a fost sau nu corupt
*/
char compute_check_sum_all(msg m){
  char sum = 0x00;
  int aux, j, i;
  for(j = 0; j < 8; j++){
    aux = 0;
    for(i = 0; i < 1399; i++){
      if((m.payload[i] & (1 << j)) == (1 << j))
        aux += 1;
    }
    if((aux % 2) == 1){
      sum = sum & (~  (1 << j));
      sum = sum | (1 << j);
    }
  }
  return sum;
}

/*
functia trimite primele window mesaje
*/
  void send_first_frames(int fd, msg m, int total_frames, int window){
    int i;
    for(i = 0; i < window; i++){
      //primul frame va trimite catre reciever dimensiunea ferestrei
      if(i == 0){
        m.len = window;
        read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
        if(read_messages == 0)
          break;
        //ce numar de mesaj e
        memcpy(m.payload + sizeof(m.payload) - 5, &i, sizeof(int));
        check_sum = compute_check_sum_all(m);
        //pe ultimul octet din payload pun check-sum
        memcpy(m.payload + SUM_POSITION, &check_sum, sizeof(char));
        //marcam mesajul ca neconfirmat
        isAcked[buff_messages] = 0;
        buffer[buff_messages] = m;
        buff_messages++;
        send_message(&m);	
      }
      else if(i == 1){
        //al doilea frame va trimite receiverului numarul total de frame-uri
        m.len = total_frames;
        read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
        if(read_messages == 0)
          break;
        //ce numar de mesaj e
        memcpy(m.payload + sizeof(m.payload) - 5, &i, sizeof(int));
        check_sum = compute_check_sum_all(m);
        memcpy(m.payload + SUM_POSITION, &check_sum, sizeof(char));
        //marcam mesajul ca neconfirmat
        isAcked[buff_messages] = 0;
        buffer[buff_messages] = m;
        buff_messages++;
        send_message(&m);		  	
      }	
      else{
        read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
        if(read_messages == 0)
          break;
        m.len = read_messages;
        //ce numar de mesaj e
        memcpy(m.payload + sizeof(m.payload) - 5, &i, sizeof(int));
        check_sum = compute_check_sum_all(m);
        memcpy(m.payload + SUM_POSITION, &check_sum, sizeof(char));
        //marcam mesajul ca neconfirmat
        isAcked[buff_messages] = 0;
        buffer[buff_messages] = m;
        buff_messages++;
        send_message(&m);
      }
    }
  }


/*
voi retrimite mesajele pentru care nu s-a primit ack
intr-un anumit timeout
*/
void resend_unAcked_frames(msg m, int next_ack, int window, int* isAcked, msg* buffer){
  int i;
  for(i = next_ack % window; i < window; i++){
    if(isAcked[i] == 0){
      //nu a fost confirmat deci il retrimitem
      m = buffer[i];
      send_message(&m);
    }
  }
  int j = 0;
  while(j < next_ack % window){
    if(isAcked[j] == 0){
      m = buffer[j];
      send_message(&m);
    }
    j++;
  }
}

/*
retrimit mesajele pentru care am primit ack, dar nu cel asteptat
*/
void resend_bad_message_ack(int ack, msg m){
  memcpy(m.payload + sizeof(m.payload) - 5, &buff_messages, 4);
  check_sum = compute_check_sum_all(m);
  memcpy(m.payload + SUM_POSITION, &check_sum, sizeof(char));
  buff_messages++;
  buffer[ack % window] = m;
  isAcked[ack % window] = 0;
  send_message(&m);
}

/*
o functie asemanatoare cu cea de mai sus dar merge doar pentru
mesajul i
*/
void resend_bad_message(int i, msg m){
  memcpy(m.payload + sizeof(m.payload) - 5, &buff_messages, 4);
  check_sum = compute_check_sum_all(m);
  memcpy(m.payload + SUM_POSITION, &check_sum, sizeof(char));
  buff_messages = buff_messages + 1;
  buffer[i] = m;
  isAcked[i] = 0;
  send_message(&m);
}

/*
verifica daca ack-ul primit este pentru mesajul asteptat
*/
int gotAllAck(int ack, int next_ack, int total_frames){
  if((ack == (total_frames - 1)) && (next_ack == (total_frames - 1)))
    return 1;
  return 0;
}

/*
verifica daca un mesaj a primit ack si daca ciclul
s-a terminat
*/
int allGood(int ack, int done){
  if(ack == 1 && done == 0)
    return 1;
  return 0;
}

int main(int argc,char** argv){
  filename = argv[1];

  SPEED = atol(argv[2]);
  DELAY = atol(argv[3]);
  BDP = SPEED * DELAY;
  
  window = (BDP * 1000)/(8 * sizeof(msg));
  
  printf("Initializam sender - ul: filename %s, speed %d, delay %d\n", filename, SPEED, DELAY);
  
  int i;
  //salvam pachetele trimise
  buffer = (msg*)malloc(window * sizeof(msg));
  //retinem daca pentru un pachet am primit sau un ack
  isAcked = (int*)malloc(window * sizeof(int));
  
  //marchez toate mesajele ca si confirmate
  for(i = 0; i < window; i++){
    isAcked[i] = 1;
  }

  init(HOST,PORT);

  int fd = open(filename, O_RDONLY);
  if(fd < 0){
    perror("Error opening file");
  }
  
  fstat(fd, &file_info);
  file_size = (int)file_info.st_size;

  //calculez numarul total de frame-uri
  int total_frames = file_size/1395;
  if(file_size % 1395 != 0)
    total_frames++;

  msg m;

  //trimit primele window mesaje
  send_first_frames(fd, m, total_frames, window);
  ack = 0;
  next_ack = 0;
  
  for(;;){
    //verific pentru ce cadre am primit timeout
    if(recv_message_timeout(&m, DELAY + 8) == -1){
      //daca nu s-a primit ack, retrimite mesajul
      resend_unAcked_frames(m, next_ack, window, isAcked, buffer);
    }
    else {
      memcpy(&ack, m.payload + sizeof(m.payload) - 5, sizeof(int));
      if(gotAllAck(ack, next_ack, total_frames) == 1)
        break;
      else {
        if(ack == next_ack) {
          read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
          m.len = read_messages;
          if(read_messages == 0) {
            //marcam mesajul ca si confirmat
            isAcked[ack % window] = 1;
          }
          else {
            resend_bad_message_ack(ack, m);
          }
          next_ack = next_ack + 1;
          done = 0;
          //tratam fiecare mesaj din window
          for(i = next_ack % window; i < window; i++){
            if(allGood(isAcked[i], done) == 1){
              read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
              m.len = read_messages;

              if(read_messages != 0){
                resend_bad_message(i, m);
              }
              if(done == 0)
                next_ack = next_ack + 1;
            }
            else
              done = 1;
          }
          
          int j = 0;
          while(j < ack % window){
            if(allGood(isAcked[j], done) == 1){
              read_messages = read(fd, m.payload, sizeof(m.payload) - 5);
              m.len = read_messages;

              if(read_messages != 0){
                resend_bad_message(j,m);
              }
              if(done == 0)
                next_ack++;
            }
            else{
              done = 1;
            }
            j++;
          }
          if(next_ack >= total_frames)
            break;
        }
        else{
          if(ack <= (next_ack + window - 1))
            isAcked[ack % window] = 1;
        }
      }
    }

  }
  close(fd);
  return 0;
}
