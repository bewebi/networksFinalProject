#include "demo_ssl_utils.h"

SSL_CTX* newSSLContext(int methodPreference)
{   const SSL_METHOD *method;
    SSL_CTX *ctx;
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_library_init();
	switch(methodPreference){
		case 1: method = TLSv1_method(); break;
		case 2: method = TLSv1_1_method(); break;
		case 3: method = TLSv1_2_method(); break;
		default: method = SSLv23_method(); break;
	}
    ctx = SSL_CTX_new(method);
    if ( ctx == NULL )
    {
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    return ctx;
}

void loadPrivateAndPublicKeys(SSL_CTX* ctx, char* cert, char* key)
{
    if ( SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0 || SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0 )
    {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    if ( !SSL_CTX_check_private_key(ctx) ){
        fprintf(stderr, "Private key does not match the public certificate\n");
	exit(1);
	}
}
