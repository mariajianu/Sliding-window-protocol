#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "link_emulator/lib.h"

#define HOST "127.0.0.1"
#define PORT 10001
#define SUM_POSITION 1399
#define N 500

msg m;
msg init_m;
int total_frames = 800;
/* frame-ul primit ultima data */
int current_frame;
/* dimensiunea ferestrei */
int window;
/*check-sum calculat */
char check_sum_recv;
/* check-sum citit din frame */
char check_sum_send;
char* filename_final;

char compute_check_sum_all(msg m){
	char sum = 0x00;
	int aux, i,j;
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

int main(int argc,char** argv){

	int next_frame = 0;
	int done;
	int fd;
	int current_messages = 0;

	init(HOST,PORT);

	filename_final = (char*)malloc(11*sizeof(char));
	filename_final = "recv_fileX";

  	fd = open(filename_final, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  
  	if(fd < 0){
    	perror("Error opening file");
  	}

  	msg buffer[N];
	int isAcked[N], i;
  	//marcam toate mesajele ca neconfirmate
  	for(i = 0; i < N; i++){
    	isAcked[i] = 0;
  	}
  	for(;;){
    	check_sum_recv = '1';
    	check_sum_send = '2';
    	while(check_sum_recv != check_sum_send) {
    		//verific pentru fieacre frame daca a fost corupt
      		recv_message(&m);
      		memcpy(&current_frame, m.payload + sizeof(m.payload) - 5, sizeof(int));
      		memcpy(&check_sum_recv, m.payload + SUM_POSITION, sizeof(char));
      		check_sum_send = compute_check_sum_all(m);
    	}
    	if(current_frame == 0) {
     		//daca e primul frame -> ia dimensiunea ferestrei
     		window = m.len;
    	}
    	if(current_frame == 1) {
      		total_frames = m.len;
    	}
    	//daca a fost primit ultimul frame
    	if(current_frame == total_frames && next_frame == total_frames){
    		//trimite ultimul ack si iesi din loop
      		send_message(&m);
      		write(fd, m.payload, m.len);
      		current_messages++;
     		break;
    	}
    	else{
      		if(current_frame == next_frame){
        		if(current_frame == 0 || current_frame == 1)
          			write(fd, m.payload, 1395);
        		else
          			write(fd, m.payload, m.len);
        		current_messages++;
        		if(current_frame == total_frames)
          			break;
        		isAcked[current_frame % window] = 0;
				send_message(&m);
        		next_frame = next_frame + 1;
        		done = 0;
        		for(i = next_frame % window; i < window; i++){
          			if(isAcked[i] == 0){
            			done = 1;
            			break;
          			}
          			else{
            			next_frame = next_frame + 1;
            			m = buffer[i];
            			write(fd, m.payload, m.len);
            			current_messages = current_messages + 1;
            			isAcked[i] = 0;
          			}
        		}
        	if(done == 0){
          		for(i = 0; i < current_frame % window; i++){
            		if(isAcked[i] == 0){
              			done = 1;
              			break;
            		}
            		else{
              			next_frame = next_frame + 1;
              			isAcked[i] = 0;
              			m = buffer[i];
              			current_messages = current_messages + 1;
              			write(fd, m.payload, m.len);
            		}
          		}
        	}
        	if(next_frame > total_frames)
         		break;
      	}
      	else{
        	if((current_frame <= next_frame + window - 1) && (current_frame <= total_frames)){
          		buffer[current_frame % window] = m;
          		isAcked[current_frame % window] = 1;
          		send_message(&m);
        	}
      	}
    }
  }

  return 0;
}
