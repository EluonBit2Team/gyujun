#include "NetCore.h"

int main()
{
    epoll_net_core net;
    init_server(&net);
    run_server(&net);
    down_server(&net);
}