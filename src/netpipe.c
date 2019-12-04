/*****************************************************************************/
/* "NetPIPE" -- Network Protocol Independent Performance Evaluator.          */
/* Copyright 1997, 1998 Iowa State University Research Foundation, Inc.      */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation.  You should have received a copy of the     */
/* GNU General Public License along with this program; if not, write to the  */
/* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */
/*                                                                           */
/* Files needed for use:                                                     */
/*     * netpipe.c       ---- Driver source                                  */
/*     * netpipe.h       ---- General include file                           */
/*     * tcp.c           ---- TCP calls source                               */
/*     * tcp.h           ---- Include file for TCP calls and data structs    */
/*     * mpi.c           ---- MPI calls source                               */
/*     * pvm.c           ---- PVM calls source                               */
/*     * pvm.h           ---- Include file for PVM calls and data structs    */
/*     * tcgmsg.c        ---- TCGMSG calls source                            */
/*     * tcgmsg.h        ---- Include file for TCGMSG calls and data structs */
/*****************************************************************************/

#include "netpipe.h"

extern char *optarg;

ArgStruct*   args_array;

/* thread function */
void *thread_fun(void *arg) {
  struct thread_ds *data = (struct thread_ds *)arg;
  int tt = data->tid;    
  ArgStruct args = args_array[tt];
  FILE        *out;           /* Output data file                          */
  double      t, t0, t1, t2, ttotal,  /* Time variables                            */
    tlast,          /* Time for the last transmission            */
    latency,        /* Network message latency                   */
    tput;

  Data        bwdata[NSAMP];  /* Bandwidth curve data                      */
  unsigned long long int totalbufflen = 0;
  int i, j, n, start, end, nrepeat, nrepeat_const;
  int         *memcache;      /* used to flush cache                       */
  int bufalign=16*1024;/* Boundary to align buffer to              */
  int         len_buf_align, streamopt;
    
  printf("thread id: %d port: %d rcv:%d\n", data->tid, args.port, args.rcv);

  Setup(&args);
  out = stdout;
  start = args.lower;
  end = args.upper;
  nrepeat_const = args.nrepeat_const;
  streamopt = args.streamopt;
  
   /* Do setup for no-cache mode, using two distinct buffers. */
   if (!args.cache)
   {
       /* Allocate dummy pool of memory to flush cache with */
       if ( (memcache = (int *)malloc(MEMSIZE)) == NULL)
       {
           perror("malloc");
           exit(1);
       }
       mymemset(memcache, 0, MEMSIZE/sizeof(int)); 

       /* Allocate large memory pools */
       // allocates r_buff and s_buff
       MyMalloc(&args, MEMSIZE+bufalign, args.soffset, args.roffset); 

       /* Save buffer addresses */
       args.s_buff_orig = args.s_buff;
       args.r_buff_orig = args.r_buff;

       /* Align buffers */
       // bufalign == 16*1024
       args.s_buff = AlignBuffer(args.s_buff, bufalign);
       args.r_buff = AlignBuffer(args.r_buff, bufalign);

       /* Initialize send buffer pointer */
       /* both soffset and roffset should be zero if we don't have any offset stuff, so this should be fine */
       args.s_ptr = args.s_buff+args.soffset;
       args.r_ptr = args.r_buff+args.roffset;
   }

   /**************************
    * Main loop of benchmark *
    **************************/
   
   if( args.tr ) printf("Now starting the main loop\n");

   Sync(&args);    /* Sync to prevent race condition in armci module */
   
   /* Calculate how many times to repeat the experiment. */
   if( args.tr )
   {
     nrepeat = nrepeat_const;
     SendRepeat(&args, nrepeat);
   }
   else if( args.rcv )
   {
     RecvRepeat(&args, &nrepeat);
   }

   if (args.randrun) {
     args.bufflen = (rand()%(end-start))+start;
     if( args.tr )
       printf("%3d: random bytes between (%d, %d) %6d times --> ",
	       n, start, end, nrepeat);
   } else {
     args.bufflen = start;
   
     if( args.tr )
       printf("%3d: %7d bytes %6d times --> ",
	       n,args.bufflen,nrepeat);
   }
   
   /* this isn't truly set up for offsets yet */
   /* Size of an aligned memory block including trailing padding */
   len_buf_align = args.bufflen;
   if(bufalign != 0)
     len_buf_align += bufalign - args.bufflen % bufalign;
   
   /* Initialize the buffers with data
    *
    * See NOTE above.
    */
   InitBufferData(&args, MEMSIZE, args.soffset, args.roffset); 
   
   /* Reset buffer pointers to beginning of pools */
   args.r_ptr = args.r_buff+args.roffset;
   args.s_ptr = args.s_buff+args.soffset;
   
   bwdata[n].t = LONGTIME;
   tput = 0;
   /* Finally, we get to transmit or receive and time */
   /* NOTE: If a module is running that uses only one process (e.g.
    * memcpy), we assume that it will always have the args.tr flag
    * set.  Thus we make some special allowances in the transmit 
    * section that are not in the receive section.
    */
   if( args.tr)
   {
     /*
       This is the transmitter: send the block TRIALS times, and
       if we are not streaming, expect the receiver to return each
       block.
     */
     for (i = 0; i < TRIALS; i++)
     {
       if(args.randrun) srand(0xdeadbeef);
       /* Flush the cache using the dummy buffer */
       if (!args.cache)
	 flushcache(memcache, MEMSIZE/sizeof(int));

       Sync(&args);
       totalbufflen = 0;
       t0 = When();
       for (j = 0; j < nrepeat; j++)
       {
	 if(args.randrun) {
	   args.bufflen = 1500; //(rand()%(end-start))+start;
	   len_buf_align = args.bufflen;
	   if(bufalign != 0)
	     len_buf_align += bufalign - args.bufflen % bufalign;
	 }
	 totalbufflen += args.bufflen;
	 printf("%d Sending %d \n", j, args.bufflen);
	 SendData(&args);
	 if (!streamopt)
	 {
	   RecvData(&args);
	   if(!args.cache)
	     AdvanceRecvPtr(&args, len_buf_align);
	 }
	 /* Wait to advance send pointer in case RecvData uses
	  * it (e.g. memcpy module).
	  */
	 if (!args.cache)
	   AdvanceSendPtr(&args, len_buf_align);
       }
       
       /* t is the 1-directional trasmission time */
       /* double tmpt = When();  */
       /* t = (tmpt - t0)/ nrepeat; */
       /* t /= 2; /\* Normal ping-pong *\/ */
       ttotal = (When() - t0) / 2;
       Reset(&args);

/* NOTE: NetPIPE does each data point TRIALS times, bouncing the message
 * nrepeats times for each trial, then reports the lowest of the TRIALS
 * times.  -Dave Turner
 */
       //bwdata[n].t = MIN(bwdata[n].t, t);
       tput = MAX(tput, (totalbufflen*CHARSIZE) / (ttotal * 1024 * 1024));
     }
   }
   else if( args.rcv )
   {
     printf("Receiving\n");
     /*
       This is the receiver: receive the block TRIALS times, and
       if we are not streaming, send the block back to the
       sender.
     */
     for (i = 0; i < TRIALS; i++)
     {
       if(args.randrun) srand(0xdeadbeef);
       /* Flush the cache using the dummy buffer */
       if (!args.cache)
	 flushcache(memcache, MEMSIZE/sizeof(int));

       Sync(&args);

       t0 = When();
       for (j = 0; j < nrepeat; j++)
       {
	 if(args.randrun) {
	   args.bufflen = 1500; //(rand()%(end-start))+start;
	   len_buf_align = args.bufflen;
	   if(bufalign != 0)
	     len_buf_align += bufalign - args.bufflen % bufalign;
	 }
	 RecvData(&args);

	 if (!args.cache)
	 { 
	   AdvanceRecvPtr(&args, len_buf_align);
	 }
                        
	 if (!streamopt)
	 {
	   SendData(&args);
	   if(!args.cache) 
	     AdvanceSendPtr(&args, len_buf_align);
	 }
       }
       t = (When() - t0)/ nrepeat;
       t /= 2; /* Normal ping-pong */
       Reset(&args);
       bwdata[n].t = MIN(bwdata[n].t, t);
     }
   }
   
   if (args.tr) {
     fprintf(stderr, " %8.2lf Mbps\n", tput);
     fprintf(out,"%8lld %8.2lf %12.8lf",
	     totalbufflen, tput, 0.0);
   }
   
    
   /* Free using original buffer addresses since we may have aligned
      r_buff and s_buff */
   if (args.cache)
     FreeBuff(args.r_buff_orig, NULL);
            
   if (!args.cache) {
        FreeBuff(args.s_buff_orig, args.r_buff_orig);
   }
   if (args.tr) fclose(out);
         
   CleanUp(&args);
   
  pthread_exit(NULL);
  
}

int main(int argc, char **argv)
{
  FILE        *out;           /* Output data file                          */
    char        s[255],s2[255],delim[255],*pstr; /* Generic strings          */
    int         *memcache;      /* used to flush cache                       */

    int         len_buf_align,  /* meaningful when args.cache is 0. buflen   */
                                /* rounded up to be divisible by 8           */
                num_buf_align;  /* meaningful when args.cache is 0. number   */
                                /* of aligned buffers in memtmp              */

    int         c, rc,              /* option index                              */
                i, j, n, nq,    /* Loop indices                              */
                asyncReceive=0, /* Pre-post a receive buffer?                */
      bufalign=16*1024,/* Boundary to align buffer to              */
                errFlag,        /* Error occurred in inner testing loop      */
                nrepeat,        /* Number of time to do the transmission     */
                nrepeat_const=0,/* Set if we are using a constant nrepeat    */
                len,            /* Number of bytes to be transmitted         */
                inc=0,          /* Increment value                           */
                perturbation=DEFPERT, /* Perturbation value                  */
                pert,
                start= 1,       /* Starting value for signature curve        */
                end=MAXINT,     /* Ending value for signature curve          */
                streamopt=0,    /* Streaming mode flag                       */
                reset_connection,/* Reset the connection between trials      */
		debug_wait=0;	/* spin and wait for a debugger		     */
   
    ArgStruct   args;           /* Arguments for all the calls               */

    double      t, t0, t1, t2, ttotal,  /* Time variables                            */
                tlast,          /* Time for the last transmission            */
                latency,        /* Network message latency                   */
                tput;

    Data        bwdata[NSAMP];  /* Bandwidth curve data                      */

    int         integCheck=0;   /* Integrity check                           */
    unsigned long long int totalbufflen = 0;
    pthread_t thr[NUM_THREADS];
    struct thread_ds thr_data[NUM_THREADS];

    /* Let modules initialize related vars, and possibly call a library init
       function that requires argc and argv */
    srand(0xdeadbeef);
    Init(&args, &argc, &argv);   /* This will set args.tr and args.rcv */

    args.preburst = 0; /* Default to not bursting preposted receives */
    args.bidir = 0; /* Turn bi-directional mode off initially */
    args.cache = 1; /* Default to use cache */
    args.host  = NULL;
    args.soffset=0; /* default to no offsets */
    args.roffset=0; 
    args.syncflag=0; /* use normal mpi_send */
    args.use_sdp=0; /* default to no SDP */
    args.port = DEFPORT; /* just in case the user doesn't set this. */
    args.randrun = 0;
    
    /* TCGMSG launches NPtcgmsg with a -master master_hostname
     * argument, so ignore all arguments and set them manually 
     * in netpipe.c instead.
     */

#if ! defined(TCGMSG)

    /* Parse the arguments. See Usage for description */
    while ((c = getopt(argc, argv, "xXSO:rIiaB2h:p:o:l:u:b:n:P:")) != -1)
    {
        switch(c)
        {
	    case 'A':
		      args.use_sdp=1;
		      break;
            case 'O':
                      strcpy(s2,optarg);
                      strcpy(delim,",");
                      if((pstr=strtok(s2,delim))!=NULL) {
                         args.soffset=atoi(pstr);
                         if((pstr=strtok((char *)NULL,delim))!=NULL)
                            args.roffset=atoi(pstr);
                         else /* only got one token */
                            args.roffset=args.soffset;
                      } else {
                         args.soffset=0; args.roffset=0;
                      }
                      printf("Transmit buffer offset: %d\nReceive buffer offset: %d\n",args.soffset,args.roffset);
                      break;
            case 'p': perturbation = atoi(optarg);
                      if( perturbation > 0 ) {
                         printf("Using a perturbation value of %d\n\n", perturbation);
                      } else {
                         perturbation = 0;
                         printf("Using no perturbations\n\n");
                      }
                      break;

            case 'B': if(integCheck == 1) {
                        fprintf(stderr, "Integrity check not supported with prepost burst\n");
                        exit(-1);
                      }
                      args.preburst = 1;
                      asyncReceive = 1;
                      printf("Preposting all receives before a timed run.\n");
                      printf("Some would consider this cheating,\n");
                      printf("but it is needed to match some vendor tests.\n"); fflush(stdout);
                      break;

            case 'I': args.cache = 0;
                      printf("Performance measured without cache effects\n\n"); fflush(stdout);
                      break;

            case 'o': strcpy(s,optarg);
                      printf("Sending output to %s\n", s); fflush(stdout);
                      break;

            case 's': streamopt = 1;
	      args.streamopt = streamopt;
                      printf("Streaming in one direction only.\n\n");
                      printf("Sockets are reset between trials to avoid\n");
                      printf("degradation from a collapsing window size.\n\n");
                      args.reset_conn = 1;
                      printf("Streaming does not provide an accurate\n");
                      printf("measurement of the latency since small\n");
                      printf("messages may get bundled together.\n\n");
                      if( args.bidir == 1 ) {
                        printf("You can't use -s and -2 together\n");
                        exit(0);
                      }
                      fflush(stdout);
                      break;

            case 'l': start = atoi(optarg);
	      args.lower = start;
                      if (start < 1)
                      {
                        fprintf(stderr,"Need a starting value >= 1\n");
                        exit(0);
                      }
                      break;

            case 'u': end = atoi(optarg);
	      args.upper = end;
                      break;

            case 'b': /* -b # resets the buffer size, -b 0 keeps system defs */
                      args.prot.sndbufsz = args.prot.rcvbufsz = atoi(optarg);
                      break;

            case '2': args.bidir = 1;    /* Both procs are transmitters */
                         /* end will be maxed at sndbufsz+rcvbufsz */
                      printf("Passing data in both directions simultaneously.\n");
                      printf("Output is for the combined bandwidth.\n");
                      printf("The socket buffer size limits the maximum test size.\n\n");
                      if( streamopt ) {
                        printf("You can't use -s and -2 together\n");
                        exit(0);
                      }
                      break;

            case 'h': args.tr = 1;       /* -h implies transmit node */
                      args.rcv = 0;
                      args.host = (char *)malloc(strlen(optarg)+1);
                      strcpy(args.host, optarg);
                      break;

            case 'i': if(args.preburst == 1) {
                        fprintf(stderr, "Integrity check not supported with prepost burst\n");
                        exit(-1);
                      }
                      integCheck = 1;
                      perturbation = 0;
                      start = sizeof(int)+1; /* Start with integer size */
                      printf("Doing an integrity check instead of measuring performance\n"); fflush(stdout);
                      break;

	    case 'P':
		      args.port = atoi(optarg);
		      break;

            case 'n': nrepeat_const = atoi(optarg);
	      args.nrepeat_const = nrepeat_const;
                      break;

            case 'r': args.reset_conn = 1;
                      printf("Resetting connection after every trial\n");
                      break;
	    case 'X': debug_wait = 1;
		      printf("Enableing debug wait!\n");
		      printf("Attach to pid %d and set debug_wait to 0 to conttinue\n", getpid());
		      break;
	case 'x':
	  args.randrun = 1;
	  break;
	  
	    default: 
                     PrintUsage(); 
                     exit(-12);
       }
   }
#endif /* ! defined TCGMSG */

   if (start > end)
   {
       fprintf(stderr, "Start MUST be LESS than end\n");
       exit(420132);
   }

   // clone struct array
   args_array = (ArgStruct*) malloc(2 * sizeof(ArgStruct));
   for(i = 0; i < NUM_THREADS; i ++) {
     thr_data[i].tid = i;
     memcpy((void*)&args_array[i], (void*)&args, sizeof(ArgStruct));
     if ((rc = pthread_create(&thr[i], NULL, thread_fun, &thr_data[i]))) {
       fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
       return EXIT_FAILURE;
     }
   }

   /* block until all threads complete */
   for (i = 0; i < NUM_THREADS; ++i) {
     pthread_join(thr[i], NULL);
   }
  
   return 0;
}


/* Return the current time in seconds, using a double precision number.      */
double When()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return ((double) tp.tv_sec + (double) tp.tv_usec * 1e-6);
}

/* 
 * The mymemset() function fills the first n integers of the memory area 
 * pointed to by ptr with the constant integer c. 
 */
void mymemset(int *ptr, int c, int n)  
{
    int i;

    for (i = 0; i < n; i++) 
        *(ptr + i) = c;
}

/* Read the first n integers of the memmory area pointed to by ptr, to flush  
 * out the cache   
 */
void flushcache(int *ptr, int n)
{
   static int flag = 0;
   int    i; 

   flag = (flag + 1) % 2; 
   if ( flag == 0) 
       for (i = 0; i < n; i++)
           *(ptr + i) = *(ptr + i) + 1;
   else
       for (i = 0; i < n; i++) 
           *(ptr + i) = *(ptr + i) - 1; 
    
}

/* For integrity check, set each integer-sized block to the next consecutive
 * integer, starting with the value 0 in the first block, and so on.  Earlier
 * we made sure the memory allocated for the buffer is of size i*sizeof(int) +
 * 1 so there is an extra byte that can be used as a flag to detect the end
 * of a receive.
 */
void SetIntegrityData(ArgStruct *p)
{
  int i;
  int num_segments;

  num_segments = p->bufflen / sizeof(int);

  for(i=0; i<num_segments; i++) {

    *( (int*)p->s_ptr + i ) = i;

  }
}

void VerifyIntegrity(ArgStruct *p)
{
  int i;
  int num_segments;
  int integrityVerified = 1;

  num_segments = p->bufflen / sizeof(int);

  for(i=0; i<num_segments; i++) {

    if( *( (int*)p->r_ptr + i )  != i ) {

      integrityVerified = 0;
      break;

    }

  }


  if(!integrityVerified) {
    
    fprintf(stderr, "Integrity check failed: Expecting %d but received %d\n",
            i, *( (int*)p->r_ptr + i ) );

    /* Dump argstruct */
    /*
    fprintf(stderr, " args struct:\n");
    fprintf(stderr, "  r_buff_orig %p [%c%c%c...]\n", p->r_buff_orig, p->r_buff_orig[i], p->r_buff_orig[i+1], p->r_buff_orig[i+2]);
    fprintf(stderr, "  r_buff      %p [%c%c%c...]\n", p->r_buff,      p->r_buff[i],      p->r_buff[i+1],      p->r_buff[i+2]);
    fprintf(stderr, "  r_ptr       %p [%c%c%c...]\n", p->r_ptr,       p->r_ptr[i],       p->r_ptr[i+1],       p->r_ptr[i+2]);
    fprintf(stderr, "  s_buff_orig %p [%c%c%c...]\n", p->s_buff_orig, p->s_buff_orig[i], p->s_buff_orig[i+1], p->s_buff_orig[i+2]);
    fprintf(stderr, "  s_buff      %p [%c%c%c...]\n", p->s_buff,      p->s_buff[i],      p->s_buff[i+1],      p->s_buff[i+2]);
    fprintf(stderr, "  s_ptr       %p [%c%c%c...]\n", p->s_ptr,       p->s_ptr[i],       p->s_ptr[i+1],       p->s_ptr[i+2]);
    */
    exit(-1);

  }

}  
    
void PrintUsage()
{
    printf("\n NETPIPE USAGE \n\n");
    printf("a: asynchronous receive (a.k.a. preposted receive)\n");
    printf("B: burst all preposts before measuring performance\n");
    printf("b: specify TCP send/receive socket buffer sizes\n");    
    printf("h: specify hostname of the receiver <-h host>\n");

    printf("I: Invalidate cache (measure performance without cache effects).\n"
           "   This simulates data coming from main memory instead of cache.\n");
    printf("i: Do an integrity check instead of measuring performance\n");
    printf("l: lower bound start value e.g. <-l 1>\n");

    printf("n: Set a constant value for number of repeats <-n 50>\n");
    printf("o: specify output filename <-o filename>\n");
    printf("O: specify transmit and optionally receive buffer offsets <-O 1,3>\n");
    printf("p: set the perturbation number <-p 1>\n"
           "   (default = 3 Bytes, set to 0 for no perturbations)\n");

    printf("r: reset sockets for every trial\n");

    printf("s: stream data in one direction only.\n");
    
    printf("u: upper bound stop value e.g. <-u 1048576>\n");
 
    printf("   The maximum test size is limited by the TCP buffer size\n");
    printf("A: Use SDP Address familty (AF_INET_SDP)\n");
    printf("\n");
}

void* AlignBuffer(void* buff, int boundary)
{
  if(boundary == 0)
    return buff;
  else
    /* char* typecast required for cc on IRIX */
    return ((char*)buff) + (boundary - ((unsigned long)buff % boundary) );
}

void AdvanceSendPtr(ArgStruct* p, int blocksize)
{
  /* Move the send buffer pointer forward if there is room */

  if(p->s_ptr + blocksize < p->s_buff + MEMSIZE - blocksize)
    
    p->s_ptr += blocksize;

  else /* Otherwise wrap around to the beginning of the aligned buffer */

    p->s_ptr = p->s_buff;
}

void AdvanceRecvPtr(ArgStruct* p, int blocksize)
{
  /* Move the send buffer pointer forward if there is room */

  if(p->r_ptr + blocksize < p->r_buff + MEMSIZE - blocksize)
    
    p->r_ptr += blocksize;

  else /* Otherwise wrap around to the beginning of the aligned buffer */

    p->r_ptr = p->r_buff;
}

void SaveRecvPtr(ArgStruct* p)
{
  /* Typecast prevents warning about loss of volatile qualifier */

  p->r_ptr_saved = (void*)p->r_ptr; 
}

void ResetRecvPtr(ArgStruct* p)
{
  p->r_ptr = p->r_ptr_saved;
}

/* This is generic across all modules */
void InitBufferData(ArgStruct *p, int nbytes, int soffset, int roffset)
{
  memset(p->r_buff, 'a', nbytes+MAX(soffset,roffset));

  /* If using cache mode, then we need to initialize the last byte
   * to the proper value since the transmitter and receiver are waiting
   * on different values to determine when the message has completely
   * arrive.
   */   
  if(p->cache)

    p->r_buff[(nbytes+MAX(soffset,roffset))-1] = 'a' + p->tr;

  /* If using no-cache mode, then we have distinct send and receive
   * buffers, so the send buffer starts out containing different values
   * from the receive buffer
   */
  else

    memset(p->s_buff, 'b', nbytes+soffset);
}

void MyMalloc(ArgStruct *p, int bufflen, int soffset, int roffset)
{
    if((p->r_buff=(char *)malloc(bufflen+MAX(soffset,roffset)))==(char *)NULL)
    {
        fprintf(stderr,"couldn't allocate memory for receive buffer\n");
        exit(-1);
    }
       /* if pcache==1, use cache, so this line happens only if flushing cache */
    
    if(!p->cache) /* Allocate second buffer if limiting cache */
      if((p->s_buff=(char *)malloc(bufflen+soffset))==(char *)NULL)
      {
          fprintf(stderr,"couldn't allocate memory for send buffer\n");
          exit(-1);
      }
}

void FreeBuff(char *buff1, char *buff2)
{
  if(buff1 != NULL)

   free(buff1);


  if(buff2 != NULL)

   free(buff2);
}
