#ifndef PROTOCOL_ADAPTER_H
#define PROTOCOL_ADAPTER_H

// ProtocolAdapter — Interfaccia astratta che disaccoppia la logica applicativa
// dal protocollo di comunicazione specifico (OCPP 1.6J). Permette di sostituire
// il protocollo senza modificare il resto del sistema.
// Requisiti: 5.1, 5.2



class ProtocolAdapter {
public:
    virtual ~ProtocolAdapter() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

};

#endif // PROTOCOL_ADAPTER_H
