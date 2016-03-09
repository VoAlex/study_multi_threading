#include <unistd.h>
#include <string>
#include <cstdio>
#include <stdlib.h> 
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
std::string chroot_dir;
static void skeleton_daemon()
{
  pid_t pid;

  /* Fork off the parent process */
  pid = fork();

  /* An error occurred */
  if (pid < 0)
    exit(EXIT_FAILURE);

  /* Success: Let the parent terminate */
  if (pid > 0)
    exit(EXIT_SUCCESS);

  /* On success: The child process becomes session leader */
  if (setsid() < 0)
    exit(EXIT_FAILURE);

  /* Catch, ignore and handle signals */
  //TODO: Implement a working signal handler */
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  /* Fork off for the second time*/
  pid = fork();

  /* An error occurred */
  if (pid < 0)
    exit(EXIT_FAILURE);

  /* Success: Let the parent terminate */
  if (pid > 0)
    exit(EXIT_SUCCESS);

  /* Set new file permissions */
  umask(0);

  /* Change the working directory to the root directory */
  /* or another appropriated directory */
  chdir(chroot_dir.c_str());

  /* Close all open file descriptors */
  int x;
  for (x = sysconf(_SC_OPEN_MAX); x>0; x--)
    {
      close (x);
    }

  /* Open the log file */
  openlog ("Http_server", LOG_PID, LOG_DAEMON);
}



struct client_info
{
  evutil_socket_t fd;
  std::string ip;
  unsigned short us_port;
};

char log_message[1024];

int ParseCmdLine(int argc, char* argv[],  std::string &s_ip, unsigned short &us_port, std::string &dir);

std::string get_response(std::string request)
{
  std::string response;
  std::string URI;
  size_t pbegin = 0;
  size_t pend = 0;
  bool b_ok = false;
  if((pend = request.find("GET",pbegin)) != request.npos)
    {
      pbegin=pend+4;

      if((pend = request.find(" ",pbegin)) != request.npos)
	{
	  URI = request.substr(pbegin, pend - pbegin);
	  //  if((pend = request.find("HTTP/1.0",pbegin)) != request.npos)
	  // {
	      char file_data[2048]={0};
	      FILE* f;
	      URI = chroot_dir + URI;
	      f = fopen(URI.c_str(),"r");
	      size_t rb = 0;
	      if(f)
		{
		  rb = fread(file_data,1,2048,f);
		  fclose(f);
		  b_ok = true;
		  response = "HTTP/1.0 200 OK\r\n";
		  response += "Date: ";
		  time_t time_now;
		  time(&time_now);
		  response += asctime(localtime(&time_now));
		  //response += "\r\n";
		  response += "Server: MyServer/1.0.0";
		  response += "\r\n";
		  response += "Content-length: ";
		  response += std::to_string(rb);
		  response += "\r\n";
		  response += "Content-Type: text/html";
		  response += "\r\n\r\n";
		  response += file_data;
		 }
	      //}
	}
    }
  if(!b_ok)
    {
      response = "HTTP/1.0 404 Not Found\r\n";
      response += "Date: ";
      time_t time_now;
      time(&time_now);
      response += asctime(localtime(&time_now));
      //response += "\r\n";
      response += "Server: MyServer/1.0.0";
      response += "\r\n";
      response += "Content-length: ";
      response += "12";
      response += "\r\n";
      response += "Content-Type: text/html";
      response += "\r\n\r\n";
      response += "Error 404!!!";
    }
  return response;
}

static void client_read_cb( struct bufferevent *bev, void *ctx)
{
    
  struct evbuffer *input = bufferevent_get_input( bev );
  struct evbuffer *output = bufferevent_get_output( bev );

  std::string client_request; 
  size_t length = evbuffer_get_length(input);
  char *data;
  data = (char*) malloc(length+1);
  evbuffer_remove(input, data, length);
  //printf("data: %s\n", data);
  data[length] = '\0';
  client_request = data;
  std::string response;
  response = get_response(client_request);
  //printf("Response: %s", response.c_str());
  evbuffer_add(output, response.c_str(), response.size());
  free(data);
}

static void client_event_cb( struct bufferevent *bev, short events, void *ctx)
{

  if( events & BEV_EVENT_ERROR)
    {
      syslog (LOG_NOTICE, "Error client_event_cb.");
      bufferevent_free(bev);
    }
  else if( events & BEV_EVENT_EOF )
    bufferevent_free(bev);
  
}

void* client_thread_fn(void *val)
{
  client_info *client = (client_info *) val;
  
  struct event_base *client_base = event_base_new();
  struct bufferevent *bev = bufferevent_socket_new( client_base, client->fd, BEV_OPT_CLOSE_ON_FREE);
  bufferevent_setcb( bev, client_read_cb, NULL, client_event_cb, NULL);
  bufferevent_enable( bev, EV_READ | EV_WRITE);

  //event_base_loop(client_base, EVLOOP_ONCE);
  event_base_dispatch(client_base);
  //sleep(5);
  //if(bev) bufferevent_free(bev);
  event_base_free(client_base);
  syslog (LOG_NOTICE, "Exit client_thread_fn.");
  return 0;
}

static void accept_conn_cb( struct evconnlistener *listener, evutil_socket_t fd,
			    struct sockaddr *address, int socklen, void *ctx)
{
  pthread_t thread;
  client_info client;
  client.fd = fd;
  client.us_port = htons(((sockaddr_in*)address)->sin_port);
  client.ip = inet_ntoa( ((sockaddr_in*)address)->sin_addr );
  sprintf(log_message,"Accept new connection IP: %s:%d!\n", client.ip.c_str(), client.us_port);
  syslog (LOG_NOTICE, log_message);

  //client_thread_fn(&client);
  if(!pthread_create(&thread, NULL, client_thread_fn, &client))
    {
        sprintf(log_message,"Create thread OK!\n");
    }
  else
    {
        sprintf(log_message,"Client thread not create!\n");
    }
    syslog (LOG_NOTICE, log_message);
    pthread_detach(thread);
}

static void accept_error_cb( struct evconnlistener *listener, void *ctx)
{
  struct event_base *base = evconnlistener_get_base( listener );
  int err = EVUTIL_SOCKET_ERROR();
  sprintf(log_message,"Error: %d = \"%s\"\n", err, evutil_socket_error_to_string(err));
  syslog (LOG_NOTICE, log_message);
  event_base_loopexit( base, NULL );
} 

int main( int argc, char* argv[])
{
  
  std::string s_ip;
  unsigned short us_port = 0;
  std::string dir;
  ParseCmdLine( argc, argv, s_ip, us_port, dir );
  
  skeleton_daemon();
  //openlog ("Http_server", LOG_PID, LOG_DAEMON);
  syslog (LOG_NOTICE, "Http_server daemon started.");

  char log_message[1024] = {0};
  sprintf(log_message,"Http_server configuration: ip: %s, port: %d, directory: %s\n", s_ip.c_str(), us_port, dir.c_str());
  syslog (LOG_NOTICE, log_message);
  //Create listener sockets
  chroot_dir = dir;
  struct event_base *base_listener = event_base_new();
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons( us_port );

    
  int r =inet_aton( s_ip.c_str(), &(sa.sin_addr));
  
  struct evconnlistener *listener = evconnlistener_new_bind( base_listener,
				    accept_conn_cb, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
				     -1, ( struct sockaddr* ) &sa, sizeof(sa));
  if(!listener)
    {
      //evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR());
      sprintf(log_message, "Http_server: Could not create listener. Error: %s",evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
      //perror("Could not create listener");
      syslog (LOG_NOTICE, log_message);
      return 1;
    }
  evconnlistener_set_error_cb( listener, accept_error_cb);
  event_base_dispatch(base_listener); 
  evconnlistener_free(listener);
  syslog (LOG_NOTICE, "Http_server daemon exit.");
  closelog();
  return 0;
}

int ParseCmdLine(int argc, char* argv[],  std::string &s_ip, unsigned short &us_port, std::string &dir)
{
  int ch;
  us_port = 0;
  std::string temp_str;
  while ((ch = getopt(argc, argv, "h:p:d:")) != -1)
  {
    switch (ch)
   {
    case 'h':
      temp_str = optarg;
      s_ip = temp_str;
      break;
    case 'p':
      temp_str = optarg;
      us_port = std::stoi(temp_str);
      break;
    case 'd':
      temp_str = optarg;
      dir = temp_str;
      break;
    case '?':
    default:
      printf("Unknown option %c !\n", ch);
   }
 }
  if(s_ip.empty()) s_ip = "127.0.0.1";
  if(!us_port) us_port = 80;
  if(dir.empty()) dir = ".";
  return 0;
}
