#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

extern "C" {
#include <freerdp/input.h>
}
#include <freerdp/freerdp.h>
#include <pthread.h>

#include "RDP.hpp"

#include "btexception.h"
#include "wsgate.h"

namespace wsgate {

    using namespace std;

    map<freerdp *, RDP *> RDP::m_instances;

    typedef struct {
        rdpContext _p;
        RDP *pRDP;
    } wsgContext;

    RDP::RDP(wspp::wshandler *h)
        : m_freerdp(freerdp_new())
          , m_rdpContext(0)
          , m_rdpInput(0)
          , m_rdpSettings(0)
          , m_bThreadLoop(false)
          , m_worker()
          , m_wshandler(h)
          , m_errMsg()
          , m_State(STATE_INITIAL)
    {
        if (!m_freerdp) {
            throw tracing::runtime_error("Could not create freerep instance");
        }
        m_instances[m_freerdp] = this;
        m_freerdp->context_size = sizeof(wsgContext);
        m_freerdp->ContextNew = cbContextNew;
        m_freerdp->ContextFree = cbContextFree;
        m_freerdp->Authenticate = cbAuthenticate;
        m_freerdp->VerifyCertificate = cbVerifyCertificate;

        freerdp_context_new(m_freerdp);
        reinterpret_cast<wsgContext *>(m_freerdp->context)->pRDP = this;
        // create worker thread
        m_bThreadLoop = true;
        if (0 != pthread_create(&m_worker, NULL, cbThreadFunc, reinterpret_cast<void *>(this))) {
            m_bThreadLoop = false;
            log::err << "Could not create RDP client thread" << endl;
        } else {
            log::debug << "Created RDP client thread" << endl;
        }
    }

    RDP::~RDP()
    {
        Disconnect();
        m_instances.erase(m_freerdp);
        freerdp_free(m_freerdp);
    }

    bool RDP::Connect(string host, int port, string user, string domain, string pass)
    {
        if (!m_rdpSettings) {
            throw tracing::runtime_error("m_rdpSettings is NULL");
        }
        if (!m_bThreadLoop) {
            throw tracing::runtime_error("worker thread has terminated");
        }
        m_rdpSettings->port = port;
        m_rdpSettings->ignore_certificate = 1;
        m_rdpSettings->hostname = strdup(host.c_str());
        m_rdpSettings->username = strdup(user.c_str());
        if (!domain.empty()) {
            m_rdpSettings->domain = strdup(domain.c_str());
        }
        if (!pass.empty()) {
            m_rdpSettings->password = strdup(pass.c_str());
        } else {
            m_rdpSettings->authentication = 0;
        }
        m_State = STATE_CONNECT;
        return true;
    }

    bool RDP::Disconnect()
    {
        if (m_bThreadLoop) {
            m_bThreadLoop = false;
            pthread_join(m_worker, NULL);
        }
        if (STATE_CONNECTED == m_State) {
            return (freerdp_disconnect(m_freerdp) != 0);
        }
        return true;
    }

    bool RDP::CheckFileDescriptor()
    {
        return ((freerdp_check_fds(m_freerdp) == 0) ? false : true);
    }

    void RDP::SendInputSynchronizeEvent(uint32_t flags)
    {
        if (!m_rdpInput) {
            throw tracing::runtime_error("m_rdpInput is NULL");
        }
        freerdp_input_send_synchronize_event(m_rdpInput, flags);
    }

    void RDP::SendInputKeyboardEvent(uint16_t flags, uint16_t code)
    {
        if (!m_rdpInput) {
            throw tracing::runtime_error("m_rdpInput is NULL");
        }
        freerdp_input_send_keyboard_event(m_rdpInput, flags, code);
    }

    void RDP::SendInputUnicodeKeyboardEvent(uint16_t flags, uint16_t code)
    {
        if (!m_rdpInput) {
            throw tracing::runtime_error("m_rdpInput is NULL");
        }
        freerdp_input_send_unicode_keyboard_event(m_rdpInput, flags, code);
    }

    void RDP::SendInputMouseEvent(uint16_t flags, uint16_t x, uint16_t y)
    {
        if (!m_rdpInput) {
            throw tracing::runtime_error("m_rdpInput is NULL");
        }
        freerdp_input_send_mouse_event(m_rdpInput, flags, x, y);
    }

    void RDP::SendInputExtendedMouseEvent(uint16_t flags, uint16_t x, uint16_t y)
    {
        if (!m_rdpInput) {
            throw tracing::runtime_error("m_rdpInput is NULL");
        }
        freerdp_input_send_extended_mouse_event(m_rdpInput, flags, x, y);
    }

    // private
    void RDP::ContextNew(freerdp *inst, rdpContext *ctx)
    {
        log::debug << "RDP::ContextNew" << endl;
        inst->PreConnect = cbPreConnect;
        inst->PostConnect = cbPostConnect;
        m_rdpContext = ctx;
        m_rdpInput = inst->input;
        m_rdpSettings = inst->settings;
    }

    // private
    void RDP::ContextFree(freerdp *, rdpContext *)
    {
        log::debug << "RDP::ContextFree" << endl;
    }

    // private
    boolean RDP::PreConnect(freerdp *)
    {
        log::debug << "RDP::PreConnect" << endl;
#if 0
        if (iUpdate != null)
        {
            update = new Update(instance->context);
            update.RegisterInterface(iUpdate);
        }

        if (iPrimaryUpdate != null)
        {
            primary = new PrimaryUpdate(instance->context);
            primary.RegisterInterface(iPrimaryUpdate);
        }
#endif

        m_rdpSettings->rfx_codec = 1;
        m_rdpSettings->fastpath_output = 1;
        m_rdpSettings->color_depth = 32;
        m_rdpSettings->frame_acknowledge = 0;
        m_rdpSettings->performance_flags = 0;
        m_rdpSettings->large_pointer = 1;
        m_rdpSettings->glyph_cache = 0;
        m_rdpSettings->bitmap_cache = 0;
        m_rdpSettings->offscreen_bitmap_cache = 0;

        return 1;
    }

    // private
    boolean RDP::PostConnect(freerdp *rdp)
    {
        log::debug << "RDP::PostConnect" << hex << rdp << dec << endl;
        return 1;
    }

    // private
    boolean RDP::Authenticate(freerdp *, char**, char**, char**)
    {
        log::debug << "RDP::Authenticate" << endl;
        return 1;
    }

    // private
    boolean RDP::VerifyCertificate(freerdp *, char*, char*, char*)
    {
        log::debug << "RDP::VerifyCertificate" << endl;
        return 1;
    }

    // private
    void RDP::ThreadFunc()
    {
        while (m_bThreadLoop) {
            if (!m_errMsg.empty()) {
                m_wshandler->send_text(m_errMsg);
                m_errMsg.clear();
            }
            switch (m_State) {
                case STATE_CONNECTED:
                    CheckFileDescriptor();
                    break;
                case STATE_CONNECT:
                    if (freerdp_connect(m_freerdp)) {
                        m_State = STATE_CONNECTED;
                        return;
                    }
                    m_State = STATE_INITIAL;
                    m_errMsg = "E:Could not connect to RDP backend.";
                    break;
            }
            usleep(100);
        }
        log::debug << "RDP client thread terminated" << endl;
    }

    // private C callback
    void RDP::cbContextNew(freerdp *inst, rdpContext *ctx)
    {
        RDP *self = m_instances[inst];
        if (self) {
            self->ContextNew(inst, ctx);
        }
    }

    void *RDP::cbThreadFunc(void *ctx)
    {
        RDP *self = reinterpret_cast<RDP *>(ctx);
        if (self) {
            self->ThreadFunc();
        }
        return NULL;
    }

    // private C callback
    void RDP::cbContextFree(freerdp *inst, rdpContext *ctx)
    {
        RDP *self = m_instances[inst];
        if (self) {
            self->ContextFree(inst, ctx);
        }
    }

    // private C callback
    boolean RDP::cbPreConnect(freerdp *inst)
    {
        RDP *self = reinterpret_cast<wsgContext *>(inst->context)->pRDP;
        if (self) {
            return self->PreConnect(inst);
        }
        return 0;
    }

    // private C callback
    boolean RDP::cbPostConnect(freerdp *inst)
    {
        RDP *self = reinterpret_cast<wsgContext *>(inst->context)->pRDP;
        if (self) {
            return self->PostConnect(inst);
        }
        return 0;
    }

    // private C callback
    boolean RDP::cbAuthenticate(freerdp *inst, char** user, char** pass, char** domain)
    {
        RDP *self = reinterpret_cast<wsgContext *>(inst->context)->pRDP;
        if (self) {
            return self->Authenticate(inst, user, pass, domain);
        }
        return 0;
    }

    // private C callback
    boolean RDP::cbVerifyCertificate(freerdp *inst, char* subject, char* issuer, char* fprint)
    {
        RDP *self = reinterpret_cast<wsgContext *>(inst->context)->pRDP;
        if (self) {
            return self->VerifyCertificate(inst, subject, issuer, fprint);
        }
        return 0;
    }

}
