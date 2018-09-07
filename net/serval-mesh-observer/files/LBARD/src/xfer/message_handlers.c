#include <stdio.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <time.h>
#include "sync.h"
#include "lbard.h"
#include "hf.h"
#include "radios.h"
#include "message_handlers.h"

message_handler message_handlers[257]={
