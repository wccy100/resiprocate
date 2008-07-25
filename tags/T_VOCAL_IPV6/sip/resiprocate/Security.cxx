#if defined(USE_SSL)

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>

#include "sip2/sipstack/SipStack.hxx"
#include "sip2/sipstack/Security.hxx"
#include "sip2/sipstack/Contents.hxx"
#include "sip2/sipstack/Pkcs7Contents.hxx"
#include "sip2/sipstack/PlainContents.hxx"
#include "sip2/util/Logger.hxx"
#include "sip2/util/Random.hxx"
#include "sip2/util/DataStream.hxx"


using namespace Vocal2;

#define VOCAL_SUBSYSTEM Subsystem::SIP



Security::Security()
{
   privateKey = NULL;
   publicCert = NULL;
   certAuthorities = NULL;
   
   static bool initDone=false;
   if ( !initDone )
   {
      initDone = true;
      
      OpenSSL_add_all_algorithms();
      ERR_load_crypto_strings();

      Random::initialize();
   }
}


Security::~Security()
{
}
  

Data 
Security::getPath( const Data& dirPath, const Data& file )
{
   Data path = dirPath;
   
   if ( path.empty() )
   {
#ifdef WIN32
      assert(0);
#else
      char* v = getenv("SIP");
      if (v)
      {
         path = Data(v);
      }
      else
      {  
          v = getenv("HOME");
          if ( v )
          {
             path = Data(v);
             path += Data("/.sip");
          }
          else
          {
             ErrLog( << "Environment variobal HOME is not set" );
             path = "/etc/sip";
          }
      }
#endif
   }
   
#ifdef WIN32
   path += Data("\\");
#else
   path += Data("/");
#endif

   assert( !file.empty() );
   
   path += file;

   DebugLog( << "Using file path " << path );
   
   return path;
}


bool 
Security::loadAllCerts( const Data& password, const Data&  dirPath )
{
   bool ok = true;
   ok = loadRootCerts( getPath( dirPath, Data("root.pem")) ) ? ok : false;
   ok = loadMyPublicCert( getPath( dirPath, Data("id.pem")) ) ? ok : false;
   ok = loadMyPrivateKey( password, getPath(dirPath,Data("id_key.pem") )) ? ok : false;

   return ok;
}
     

bool 
Security::loadMyPublicCert( const Data&  filePath )
{
   assert( !filePath.empty() );
   
   FILE* fp = fopen(filePath.c_str(),"r");
   if ( !fp )
   {
      ErrLog( << "Could not read public cert from " << filePath );
      return false;
   }
   
   publicCert = PEM_read_X509(fp,NULL,NULL,NULL);
   if (!publicCert)
   {
      ErrLog( << "Error reading contents of public cert file " << filePath );
      return false;
   }
   
   InfoLog( << "Loaded public cert from " << filePath );
   
   return true;
}


bool 
Security::loadRootCerts(  const Data& filePath )
{ 
   assert( !filePath.empty() );
   
   certAuthorities = X509_STORE_new();
   assert( certAuthorities );
   
   if ( X509_STORE_load_locations(certAuthorities,filePath.c_str(),NULL) != 1 )
   {  
      ErrLog( << "Error reading contents of root cert file " << filePath );
      return false;
   }
   
   InfoLog( << "Loaded public CAs from " << filePath );

   return true;
}


bool 
Security::loadMyPrivateKey( const Data& password, const Data&  filePath )
{
   assert( !filePath.empty() );
   
   FILE* fp = fopen(filePath.c_str(),"r");
   if ( !fp )
   {
      ErrLog( << "Could not read private key from " << filePath );
      return false;
   }
   
   //DebugLog( "password is " << password );
   
   privateKey = PEM_read_PrivateKey(fp,NULL,NULL,(void*)password.c_str());
   if (!privateKey)
   {
      ErrLog( << "Error reading contents of private key file " << filePath );

      while (1)
      {
         const char* file;
         int line;
         
         unsigned long code = ERR_get_error_line(&file,&line);
         if ( code == 0 )
         {
            break;
         }

         char buf[256];
         ERR_error_string_n(code,buf,sizeof(buf));
         ErrLog( << buf  );
         DebugLog( << "Error code = " << code << " file=" << file << " line=" << line );
      }
      
      return false;
   }
   
   InfoLog( << "Loaded private key from << " << filePath );
   
   return true;
}



Pkcs7Contents* 
Security::sign( Contents* bodyIn )
{
   assert( bodyIn );

   int flags;
   flags |= PKCS7_BINARY;
   
   Data bodyData;
   oDataStream strm(bodyData);
#if 1
   strm << "Content-Type: " << bodyIn->getType() << Symbols::CRLF;
   strm << Symbols::CRLF;
#endif
   bodyIn->encode( strm );
   strm.flush();
   
   DebugLog( << "body data to sign is <" << bodyData << ">" );
      
   const char* p = bodyData.data();
   int s = bodyData.size();
   
   BIO* in;
   in = BIO_new_mem_buf( (void*)p,s);
   assert(in);
   DebugLog( << "ceated in BIO");
    
   BIO* out;
   out = BIO_new(BIO_s_mem());
   assert(out);
   DebugLog( << "created out BIO" );
     
   STACK_OF(X509)* chain=NULL;
   chain = sk_X509_new_null();
   assert(chain);

   DebugLog( << "checking" );
   assert( publicCert );
   assert( privateKey );
   
   int i = X509_check_private_key(publicCert, privateKey);
   DebugLog( << "checked cert and key ret=" << i  );
   
   PKCS7* pkcs7 = PKCS7_sign( publicCert, privateKey, chain, in, flags);
   if ( !pkcs7 )
   {
       ErrLog( << "Error creating PKCS7 signing object" );
      return NULL;
   }
    DebugLog( << "created PKCS7 sign object " );

#if 0
   if ( SMIME_write_PKCS7(out,pkcs7,in,0) != 1 )
   {
      ErrLog( << "Error doind S/MIME write of signed object" );
      return NULL;
   }
   DebugLog( << "created SMIME write object" );
#else
   i2d_PKCS7_bio(out,pkcs7);
#endif
   BIO_flush(out);
   
   char* outBuf=NULL;
   long size = BIO_get_mem_data(out,&outBuf);
   assert( size > 0 );
   
   Data outData(outBuf,size);
  
   InfoLog( << "Signed body size is <" << outData.size() << ">" );
   //InfoLog( << "Signed body is <" << outData.escaped() << ">" );

   Pkcs7Contents* outBody = new Pkcs7Contents( outData );
   assert( outBody );

   return outBody;
}


Pkcs7Contents* 
Security::encrypt( Contents* bodyIn, const Data& recipCertName )
{
   assert( bodyIn );
   
   int flags = 0 ;  
   flags |= PKCS7_BINARY;
   
   Data bodyData;
   oDataStream strm(bodyData);
#if 1
   strm << "Content-Type: " << bodyIn->getType() << Symbols::CRLF;
   strm << Symbols::CRLF;
#endif
   bodyIn->encode( strm );
   strm.flush();
   
   DebugLog( << "body data to encrypt is <" << bodyData << ">" );
      
   const char* p = bodyData.data();
   int s = bodyData.size();
   
   BIO* in;
   in = BIO_new_mem_buf( (void*)p,s);
   assert(in);
   DebugLog( << "ceated in BIO");
    
   BIO* out;
   out = BIO_new(BIO_s_mem());
   assert(out);
   DebugLog( << "created out BIO" );

   InfoLog( << "target cert name is " << recipCertName );
   X509* cert = publicCert; // !cj! this is the wrong one - need to load right one
   
   STACK_OF(X509) *certs;
   certs = sk_X509_new_null();
   assert(certs);
   assert( cert );
   sk_X509_push(certs, cert);
   
   EVP_CIPHER* cipher = EVP_des_ede3_cbc();
   assert( cipher );
   
   PKCS7* pkcs7 = PKCS7_encrypt( certs, in, cipher, flags);
   if ( !pkcs7 )
   {
       ErrLog( << "Error creating PKCS7 encrypt object" );
      return NULL;
   }
    DebugLog( << "created PKCS7 encrypt object " );

#if 0
   if ( SMIME_write_PKCS7(out,pkcs7,in,0) != 1 )
   {
      ErrLog( << "Error doind S/MIME write of signed object" );
      return NULL;
   }
   DebugLog( << "created SMIME write object" );
#else
   i2d_PKCS7_bio(out,pkcs7);
#endif
   BIO_flush(out);
   
   char* outBuf=NULL;
   long size = BIO_get_mem_data(out,&outBuf);
   assert( size > 0 );
   
   Data outData(outBuf,size);
  
   InfoLog( << "Encrypted body size is <" << outData.size() << ">" );
   //InfoLog( << "Encrypted body is <" << outData.escaped() << ">" );

   Pkcs7Contents* outBody = new Pkcs7Contents( outData );
   assert( outBody );

   return outBody;
   
}


Contents* 
Security::uncode( Pkcs7Contents* sBody, Data* signedBy, SignatureStatus* sigStat, bool* encrypted )
{
   SignatureStatus localSigStat;
   if (!sigStat) sigStat=&localSigStat;
   bool localEncyped;
   if (!encrypted) encrypted=&localEncyped;
   Data localSignedby;
   if (!signedBy) signedBy=&localSignedby;

   *encrypted = false;
   *sigStat = none;
   *signedBy = Data::Empty;
   
   InfoLog( << "Calling first layer of Security:uncode");
   Contents* outBody = uncodeSingle( sBody, true, signedBy, sigStat, encrypted );
   if ( (!outBody) && (*sigStat==isBad ) )
   {
      InfoLog( << "Retry first layer of Security:uncode");
      outBody = uncodeSingle( sBody, false, signedBy, sigStat, encrypted ); 
   }
   
   Pkcs7Contents* recuriveBody = dynamic_cast<Pkcs7Contents*>( outBody );
   if ( recuriveBody )
   {
      InfoLog( << "Calling Second layer of Security:uncode");

      outBody = uncodeSingle( recuriveBody, true, signedBy, sigStat, encrypted );
      if ( (!outBody) && (*sigStat==isBad ) )
      {
         InfoLog( << "Retry Second layer of Security:uncode");
         outBody = uncodeSingle( recuriveBody, false, signedBy, sigStat, encrypted ); 
      }
      
      delete recuriveBody;
   }
   
   return outBody;
}


Contents* 
Security::uncodeSingle( Pkcs7Contents* sBody, bool verifySig,  
                        Data* signedBy, SignatureStatus* sigStatus, bool* encrypted )
{
   int flags=0;
   flags |= PKCS7_BINARY;
   
   // for now, assume that this is only a singed message
   assert( sBody );
   
   //const char* p = sBody->text().data();
   const char* p = sBody->text().c_str();
   int   s = sBody->text().size();
   //InfoLog( << "uncode body = " << sBody->text().escaped() );
   InfoLog( << "uncode body size = " << s );
   
   BIO* in;
   in = BIO_new_mem_buf( (void*)p,s);
   assert(in);
   InfoLog( << "ceated in BIO");
    
   BIO* out;
   out = BIO_new(BIO_s_mem());
   assert(out);
   InfoLog( << "created out BIO" );

#if 0
   BIO* pkcs7Bio=NULL;
   PKCS7* pkcs7 = SMIME_read_PKCS7(in,&pkcs7Bio);
   if ( !pkcs7 )
   {
      ErrLog( << "Problems doing SMIME_read_PKCS7" );
      return NULL;
   }
   if ( pkcs7Bio )
   {
      ErrLog( << "Can not deal with mutlipart mime version stuff " );
      return NULL;
   }  
#else
   BIO* pkcs7Bio=NULL;
   PKCS7* pkcs7 = d2i_PKCS7_bio(in, NULL);
   if ( !pkcs7 )
   {
      ErrLog( << "Problems doing decode of PKCS7 object" );

      while (1)
           {
              const char* file;
              int line;
              
              unsigned long code = ERR_get_error_line(&file,&line);
              if ( code == 0 )
              {
                 break;
              }
              
              char buf[256];
              ERR_error_string_n(code,buf,sizeof(buf));
              ErrLog( << buf  );
              InfoLog( << "Error code = " << code << " file=" << file << " line=" << line );
           }
           
      return NULL;
   }
   BIO_flush(in);
#endif
   
   int type=OBJ_obj2nid(pkcs7->type);
   switch (type)
   {
      case NID_pkcs7_signed:
         InfoLog( << "data is pkcs7 signed" );
         break;
      case NID_pkcs7_signedAndEnveloped:
         InfoLog( << "data is pkcs7 signed and enveloped" );
         break;
      case NID_pkcs7_enveloped:
         InfoLog( << "data is pkcs7 enveloped" );
         break;
      case NID_pkcs7_data:
         InfoLog( << "data i pkcs7 data" );
         break;
      case NID_pkcs7_encrypted:
         InfoLog( << "data is pkcs7 encrypted " );
         break;
      case NID_pkcs7_digest:
         InfoLog( << "data is pkcs7 digest" );
         break;
      default:
         InfoLog( << "Unkown pkcs7 type" );
         break;
   }

   STACK_OF(X509)* signers = PKCS7_get0_signers(pkcs7, NULL/*certs*/, 0/*flags*/ );
   for (int i=0; i<sk_X509_num(signers); i++)
   {
      X509* x = sk_X509_value(signers,i);

      STACK* emails = X509_get1_email(x);

      for ( int j=0; j<sk_num(emails); j++)
      {
         char* e = sk_value(emails,j);
         InfoLog("email field of signing cert is <" << e << ">" );
         if ( signedBy)
         {
            *signedBy = Data(e);
         }
      }
   }

   STACK_OF(X509)* certs;
   certs = sk_X509_new_null();
   assert( certs );
   
   if ( !verifySig )
   {
      flags |= PKCS7_NOVERIFY;
   }
   
   assert( certAuthorities );
   
   switch (type)
   {
     case NID_pkcs7_signedAndEnveloped:
     {
        assert(0);
     }
     break;
     
     case NID_pkcs7_enveloped:
     {
        if ( PKCS7_decrypt(pkcs7, privateKey, publicCert, out, flags ) != 1 )
        {
           ErrLog( << "Problems doing PKCS7_decrypt" );
           while (1)
           {
              const char* file;
              int line;
              
              unsigned long code = ERR_get_error_line(&file,&line);
              if ( code == 0 )
              {
                 break;
              }
              
              char buf[256];
              ERR_error_string_n(code,buf,sizeof(buf));
              ErrLog( << buf  );
              InfoLog( << "Error code = " << code << " file=" << file << " line=" << line );
           }
           
           return NULL;
        }
        if ( encrypted )
        {
           *encrypted = true;
        }
     }
     break;
      
      case NID_pkcs7_signed:
      {
         if ( PKCS7_verify(pkcs7, certs, certAuthorities, pkcs7Bio, out, flags ) != 1 )
         {
            ErrLog( << "Problems doing PKCS7_verify" );

            if ( sigStatus )
            {
               *sigStatus = isBad;
            }

            while (1)
            {
               const char* file;
               int line;
               
               unsigned long code = ERR_get_error_line(&file,&line);
               if ( code == 0 )
               {
                  break;
               }
               
               char buf[256];
               ERR_error_string_n(code,buf,sizeof(buf));
               ErrLog( << buf  );
               InfoLog( << "Error code = " << code << " file=" << file << " line=" << line );
            }
            
            return NULL;
         }
         if ( sigStatus )
         {
            if ( flags & PKCS7_NOVERIFY )
            {
               *sigStatus = notTrusted;
            }
            else
            {
               if (false) // !cj! TODO look for this cert in store
               {
                  *sigStatus = trusted;
               }
               else
               {
                  *sigStatus = caTrusted;
               }
            }
         }
      }
      break;
      
      default:
         ErrLog("Got PKCS7 data that could not be handled type=" << type );
         return NULL;
   }
      
   BIO_flush(out);
   char* outBuf=NULL;
   long size = BIO_get_mem_data(out,&outBuf);
   assert( size >= 0 );
   
   Data outData(outBuf,size);
   
   //InfoLog( << "uncodec body is <" << outData << ">" );

   // parse out the header information and form new body.
   // !cj! this is a really crappy parser - shoudl do proper mime stuff
   ParseBuffer pb( outData.data(), outData.size() );
   pb.skipToChar(Symbols::COLON[0]);
   pb.skipChar();
   
   Mime mime;
   mime.parse(pb);

   const char* anchor = pb.skipToChars(Symbols::CRLFCRLF);
   if ( Contents::getFactoryMap().find(mime) == Contents::getFactoryMap().end())
   {
      ErrLog( << "Don't know how to deal with MIME type " << mime );
      return NULL;
   }
   
   return Contents::createContents(mime, anchor, pb);
}

#endif

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 */