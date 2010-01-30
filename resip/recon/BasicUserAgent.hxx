#if !defined(BasicUserAgent_hxx)
#define BasicUserAgent_hxx

#include "UserAgent.hxx"
#include "ConversationProfile.hxx"
#include "UserAgentMasterProfile.hxx"
#include "RegistrationManager.hxx"
#include "ApplicationTimers.hxx"
#include "RTPPortAllocator.hxx"

#include <resip/stack/InterruptableStackThread.hxx>
#include <resip/stack/SelectInterruptor.hxx>
#include <resip/dum/MasterProfile.hxx>
#include <resip/dum/RegistrationHandler.hxx>
#include <resip/dum/SubscriptionHandler.hxx>
#include <resip/dum/DumShutdownHandler.hxx>
#include <resip/dum/DialogUsageManager.hxx>
#include <rutil/Log.hxx>
#include <rutil/SharedPtr.hxx>
#include <rutil/Mutex.hxx>

#ifdef WIN32
   #define sleepMs(t) Sleep(t)
#else
   #define sleepMs(t) usleep(t*1000)
#endif

namespace recon
{
class UserAgentShutdownCmd;
class SetActiveConversationProfileCmd;
class UserAgentClientSubscription;
class UserAgentRegistration;
class ConversationManager;

typedef unsigned int SubscriptionHandle;

/**
  This class is one of two main classes of concern to an application
  using the UserAgent library.  This class should be subclassed by 
  the application and the UserAgent handlers should be implemented
  by it.

  This class is responsible for handling tasks that are not directly
  related to managing the Conversations themselves.  All conversation
  management is done via the ConversationManager class.  
  
  This class handles tasks such as:
  - Startup, Process Loop, and shutdown handling
  - Handling user agent settings (UserAgentMasterProfile)
  - Handling and dispatching Application Timers
  - Management of Conversation Profiles and Registrations
  - Management of Subscriptions

  Author: Scott Godin (sgodin AT SipSpectrum DOT com)
*/

class BasicUserAgent : public recon::UserAgent,
                       public resip::ClientRegistrationHandler,
                       public resip::ClientSubscriptionHandler,
                       public resip::DumShutdownHandler
{
public:

   /**
     Constructor

     @param conversationManager Application subclassed Conversation 
                                Manager
     @param masterProfile       Object containing useragent settings
   */
   BasicUserAgent(resip::SharedPtr<UserAgentMasterProfile> masterProfile, MediaStack* mediaStack, CodecFactory* codecFactory);
   virtual ~BasicUserAgent();

   /**
     Starts the SIP stack thread.
     
     @note This should be called before calling process() in a loop

     @warning Ensure you have added at least one Conversation Profile 
              with defaultOutgoing set to true before calling this. 
   */
   void startup(); 

   /**
     This should be called in a loop to give process cycles to the BasicUserAgent.

     @param timeoutMs Will return after timeoutMs if nothing to do.
                      Application can do some work, but should call
                      process again ASAP.
   */
   void process(int timeoutMs); // call this in a loop

   /**
     Used to initiate a shutdown of the useragent.  This function blocks 
     until the shutdown is complete.  

     @note There should not be an active process request when this 
           is called.
   */
   void shutdown();

   /**
     Used to initiate a snapshot of the existing DNS entries in the
     cache to the logging subsystem.
   */
   void logDnsCache();

   /**
     Used to clear the existing DNS entries in the cache.
   */
   void clearDnsCache();

   /**
     Sets the ConversationManager.
   */
   void setConversationManager(ConversationManager* conversationManager);

   /**
     Retrieves the ConversationManager set using setConversationManager().

     @return Pointer to the conversation manager
   */
   ConversationManager* getConversationManager();

   // Utility Methods ////////////////////////////////////////////////////////////
   typedef enum 
   {
      SubsystemAll,
      SubsystemContents,
      SubsystemDns,
      SubsystemDum,
      SubsystemSdp,
      SubsystemSip,
      SubsystemTransaction,
      SubsystemTransport,
      SubsystemStats,
      SubsystemRecon,
      SubsystemFlowManager,
      SubsystemReTurn
   } LoggingSubsystem;

   /**
     Static method that sets the logging level for a particular subsystem, 
     or all subsystems.

     @param level Logging level to set
     @param subsystem Subsystem to set level on

     @note: Use BasicUserAgent::SubsystemAll to set the logging level on all 
            subsystems
   */
   static void setLogLevel(resip::Log::Level level, LoggingSubsystem subsystem=SubsystemAll);

   /**
    * Returns a shared pointer to the application timer service. Use this
    * in order to provide callbacks from timer events into the application.
    */
   resip::SharedPtr<ApplicationTimers> getTimers();
   
   /**
     Requests that the user agent create and manage an event subscription.  
     When an subscribed event is received the onSubscriptionNotify callback 
     is used.  If the subscription is terminated by the server, the application 
     or due to network failure the onSubscriptionTerminated callback is used.

     Applications should override this BasicUserAgent class and callbacks in order 
     to use subscriptions.

     @param eventType Event type we are subscribing to
     @param target    A URI representing the location of the subscription server
     @param subscriptionTime Requested time that the subscription should stay active, 
                      before sending a refresh request is required.
     @param mimeType  The mime type of event body expected
     @param convProfile The profile representing the source endpoint for the subscription session

     !jjg! passing a 'ConversationProfileHandle' here seems odd...
           we should probably revisit whether or not 'ConversationProfile' is a good name, 
           and whether or not ConversationManager should track 'ConversationProfiles' if they
           are going to be used like this for other purposes (e.g. subscription sessions)
   */
   SubscriptionHandle createSubscription(const resip::Data& eventType, const resip::NameAddr& target, unsigned int subscriptionTime, const resip::Mime& mimeType, ConversationProfileHandle convProfile); // thread safe

   /**
     Destroys an existing subscriptions.  Unsubscribe request is sent, 
     if required.

     @param handle Subscription handle to destroy
   */
   void destroySubscription(SubscriptionHandle handle); 

   ////////////////////////////////////////////////////////////////////
   // BasicUserAgent Handlers //////////////////////////////////////////////
   ////////////////////////////////////////////////////////////////////

   /**
     Callback used when a subscription has received an event notification 
     from the server.

     @note An application should override this method if it uses
           createSubscription.

     @param handle Subscription handle that event applies to
     @param notifyData Data representation of the event received
   */
   virtual void onSubscriptionNotify(SubscriptionHandle handle, const resip::Data& notifyData); 

   /**
     Callback used when a subscription is terminated.

     @note An application should override this method if it uses
           createSubscription.

     @param handle Subscription handle that terminated
     @param statusCode The status code that caused termination.  If
                       application terminated, then statusCode will be 0.
   */
   virtual void onSubscriptionTerminated(SubscriptionHandle handle, unsigned int statusCode);   

   /**
     Implementing recon::UserAgent
   */
   virtual resip::DialogUsageManager* getDialogUsageManager() { return &mDum; }
   virtual recon::MediaStack* getMediaStack() { return mMediaStack; }
   virtual recon::CodecFactory* getCodecFactory() { return mCodecFactory; }
   virtual recon::RegistrationManager* getRegistrationManager() { return mRegManager.get(); }

protected:
   // Shutdown Handler ////////////////////////////////////////////////////////////
   void onDumCanBeDeleted();

   // Registration Handler ////////////////////////////////////////////////////////
   virtual void onSuccess(resip::ClientRegistrationHandle h, const resip::SipMessage& response);
   virtual void onFailure(resip::ClientRegistrationHandle h, const resip::SipMessage& response);
   virtual void onRemoved(resip::ClientRegistrationHandle h, const resip::SipMessage& response);
   virtual int onRequestRetry(resip::ClientRegistrationHandle h, int retryMinimum, const resip::SipMessage& msg);

   // ClientSubscriptionHandler ///////////////////////////////////////////////////
   virtual void onUpdatePending(resip::ClientSubscriptionHandle h, const resip::SipMessage& notify, bool outOfOrder);
   virtual void onUpdateActive(resip::ClientSubscriptionHandle h, const resip::SipMessage& notify, bool outOfOrder);
   virtual void onUpdateExtension(resip::ClientSubscriptionHandle, const resip::SipMessage& notify, bool outOfOrder);
   virtual void onTerminated(resip::ClientSubscriptionHandle h, const resip::SipMessage* notify);
   virtual void onNewSubscription(resip::ClientSubscriptionHandle h, const resip::SipMessage& notify);
   virtual int  onRequestRetry(resip::ClientSubscriptionHandle h, int retryMinimum, const resip::SipMessage& notify);

private:
   friend class ConversationManager;
   friend class UserAgentShutdownCmd;
   friend class CreateConversationCmd;
   friend class CreateSubscriptionCmd;
   friend class DestroySubscriptionCmd;
   friend class MediaResourceParticipant;

   // Note:  In general the following fns are not thread safe and must be called from dum process 
   //        loop only
   friend class RemoteParticipant;
   friend class DefaultDialogSet;
   friend class RemoteParticipantDialogSet;
   resip::SharedPtr<UserAgentMasterProfile> getUserAgentMasterProfile();

   void addTransports();
   void shutdownImpl(); 
   void createSubscriptionImpl(SubscriptionHandle handle, const resip::Data& eventType, const resip::NameAddr& target, unsigned int subscriptionTime, const resip::Mime& mimeType, resip::SharedPtr<ConversationProfile> convProfile);
   void destroySubscriptionImpl(SubscriptionHandle handle);

   // Subscription storage
   friend class UserAgentClientSubscription;
   typedef std::map<SubscriptionHandle, UserAgentClientSubscription *> SubscriptionMap;
   SubscriptionMap mSubscriptions;
   resip::Mutex mSubscriptionHandleMutex;
   SubscriptionHandle mCurrentSubscriptionHandle;
   SubscriptionHandle getNewSubscriptionHandle();  // thread safe
   void registerSubscription(UserAgentClientSubscription *);
   void unregisterSubscription(UserAgentClientSubscription *);

   ConversationManager* mConversationManager;
   resip::SharedPtr<UserAgentMasterProfile> mProfile;
   resip::Security* mSecurity;
   resip::SelectInterruptor mSelectInterruptor;
   resip::SipStack mStack;
   resip::DialogUsageManager mDum;
   resip::InterruptableStackThread mStackThread;
   volatile bool mDumShutdown;
   resip::NameAddr* mPublicBinding;

   resip::SharedPtr<ApplicationTimers> mApplicationTimers;

   recon::MediaStack* mMediaStack;
   recon::CodecFactory* mCodecFactory;
   resip::SharedPtr<recon::RegistrationManager> mRegManager;
};
 
}

#endif


/* ====================================================================

 Copyright (c) 2007-2008, Plantronics, Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are 
 met:

 1. Redistributions of source code must retain the above copyright 
    notice, this list of conditions and the following disclaimer. 

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution. 

 3. Neither the name of Plantronics nor the names of its contributors 
    may be used to endorse or promote products derived from this 
    software without specific prior written permission. 

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ==================================================================== */
