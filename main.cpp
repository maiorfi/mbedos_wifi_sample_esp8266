#include "mbed.h"

#include "SWO.h"

// buffer sizes for socket-related operations (read/write)
#define SOCKET_SEND_BUFFER_SIZE 32
#define SOCKET_RECEIVE_BUFFER_SIZE 32

// host, port of (sample) TCP server/listener
#define TCP_SERVER_ADDRESS "broker.mqtt.it"
#define TCP_SERVER_PORT 8888

SWO_Channel swo("channel");

static DigitalOut led1(LED1, false);

static InterruptIn btn(BUTTON1);

// reference to "WiFiInterface" object that will provide for network-related operation (connect, read/write, disconnect)
WiFiInterface *wifi;

// Thread/EventQueue pair that will manage network operations (EventQueue ensures that "atomic" operations will not be interrupted until completion)
static Thread s_thread_manage_network;
static EventQueue s_eq_manage_network;

static uint32_t counter=0;
char sbuffer[SOCKET_SEND_BUFFER_SIZE];
char rbuffer[SOCKET_RECEIVE_BUFFER_SIZE];

// set of states for a simple finite-state-machine whose main purpose is to keep network connection "as much open as possible" (automatic reconnect)  
typedef enum _ConnectionState
{
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTED
} ConnectionState;

// current state
static ConnectionState s_connectionState=NETWORK_STATE_DISCONNECTED;

// this callback (scheduled every 5 secs) implements network reconnect policy
void event_proc_manage_network_connection()
{
    if(s_connectionState==NETWORK_STATE_CONNECTED) return;

    swo.printf("> Initializing Network...\n\n");

    wifi = WiFiInterface::get_default_instance();

    if (!wifi) {
        swo.printf("ERROR: No WiFiInterface found.\n");
        return;
    }

    swo.printf("\nConnecting to %s...\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
        swo.printf("\nConnection error: %d\n", ret);
        return;
    }

    swo.printf("> ...connection SUCCEEDED\n\n");
    swo.printf("MAC: %s\n", wifi->get_mac_address());
    swo.printf("IP: %s\n", wifi->get_ip_address());
    swo.printf("Netmask: %s\n", wifi->get_netmask());
    swo.printf("Gateway: %s\n", wifi->get_gateway());
    swo.printf("RSSI: %d\n\n", wifi->get_rssi());

    s_connectionState=NETWORK_STATE_CONNECTED;
}

// this callback (scheduled every second) actually implement a request+reply transaction sample
void event_proc_send_and_receive_data(const char* message_type)
{
    if(s_connectionState!=NETWORK_STATE_CONNECTED) return;

    TCPSocket socket;
    nsapi_error_t socket_operation_return_value;

    // step 1/2: request (open, connect, send...close and return on error)
    sprintf(sbuffer, "%s %lu\r", message_type, ++counter);
    nsapi_size_t size = strlen(sbuffer);

    swo.printf("Sending %d bytes to %s:%d...\n", size, TCP_SERVER_ADDRESS, TCP_SERVER_PORT);

    socket.set_timeout(3000);
    socket.open(wifi);
    
    socket_operation_return_value = socket.connect(TCP_SERVER_ADDRESS, TCP_SERVER_PORT);
    
    if(socket_operation_return_value != 0)
    {
        swo.printf("...error in socket.connect(): %d\n", socket_operation_return_value);
        socket.close();
        wifi->disconnect();
        s_connectionState=NETWORK_STATE_DISCONNECTED;
        return;
    }

    socket_operation_return_value = 0;
    
    while(size)
    {
        socket_operation_return_value = socket.send(sbuffer + socket_operation_return_value, size);

        if (socket_operation_return_value < 0)
        {
            swo.printf("...error sending data: %d\n", socket_operation_return_value);
            socket.close();
            return;
        }
        else
        {
            size -= socket_operation_return_value;
        }
    }

    sbuffer[strlen(sbuffer)-1]='\0';

    swo.printf("...sent '%s'\n", sbuffer);

    // step 2/2: receive reply (receive, close...close and return on error)
    socket_operation_return_value = socket.recv(rbuffer, sizeof rbuffer);
    
    if (socket_operation_return_value < 0)
    {
        swo.printf("...error receiving data: %d\n", socket_operation_return_value);
    }
    else
    {
        // clear CR/LF chars for a cleaner debug terminal output
        rbuffer[socket_operation_return_value]='\0';
        if(rbuffer[socket_operation_return_value-1]=='\n' || rbuffer[socket_operation_return_value-1]=='\r') rbuffer[socket_operation_return_value-1]='\0';
        if(rbuffer[socket_operation_return_value-2]=='\n' || rbuffer[socket_operation_return_value-2]=='\r') rbuffer[socket_operation_return_value-2]='\0';

        swo.printf("...received: '%s'\n", rbuffer);
    }

    socket.close();

    // id led is toggling everything is working as expected
    led1.write(!led1.read());
}

// in case of an hardware interrupt, the isr routine schedules (on EventQueue dedicated to network operations)
// a call to event_proc_send_and_receive_data(), but with an argument ("btn") different from one used in periodic request+reply ("test", see below)
void btn_interrupt_handler()
{
    s_eq_manage_network.call(event_proc_send_and_receive_data, "btn");
}

int main()
{
    swo.printf(" -------- WIFI sample started --------\n\n");
    
    s_eq_manage_network.call_every(5000, event_proc_manage_network_connection);
    s_eq_manage_network.call_every(1000, event_proc_send_and_receive_data, "test");

    btn.fall(&btn_interrupt_handler);

    s_thread_manage_network.start(callback(&s_eq_manage_network, &EventQueue::dispatch_forever));
}
