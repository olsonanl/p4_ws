
#include "AuthToken.h"
#include "SigningCerts.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include <ctime>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <map>

#include <boost/algorithm/string.hpp>

static void SSL_init()
{
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    OPENSSL_config(NULL);
}

static void SSL_finish()
{
    ERR_free_strings();
    EVP_cleanup();
    OPENSSL_no_config();    
}

int main(int argc, char **argv)
{
    SSL_init();
    
    AuthToken tok;
    std::ifstream tstr("/home/olson/.patric_token");
    tstr >> tok;
    std::cerr << "token for " << tok.user() << " expired: " << tok.is_expired() << "\n";

    SigningCerts certs;

    std::cerr << "valid: " << certs.validate(tok) << "\n";
    
    SSL_finish();
    
    return 0;
}
