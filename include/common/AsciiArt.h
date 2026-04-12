#ifndef INCLUDE_COMMON_ASCIIART_H_
#define INCLUDE_COMMON_ASCIIART_H_

#include <iostream>

void printBanner()
{
    std::cout << R"(

   ____   ____      ____ _   _    _    ____   ____ _____ ____  
  |  _ \ / ___|    / ___| | | |  / \  |  _ \ / ___| ____|  _   
  | |_) | |  _ ____| |   | |_| | / _ \ | |_) | |  _|  _| | |_) |
  |  __/| |_| |____| |___|  _  |/ ___ \|  _ <| |_| | |___|  _ < 
  |_|    \____|     \____|_| |_/_/   \_\_| \_\\____|_____|_| \_ 

            E V   C H A R G E R   S I M U L A T O R

)" << std::endl;
}


#endif /* INCLUDE_COMMON_ASCIIART_H_ */
