#include <openssl/ssl.h>
#include <openssl/err.h>

SSL_CTX* newSSLContext();
void loadPrivateAndPublicKeys(SSL_CTX* ctx, char* cert, char* key);
