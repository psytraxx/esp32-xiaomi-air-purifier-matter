#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void run_protocol_tests(void);
void run_queue_tests(void);
void run_session_tests(void);
void run_model_tests(void);
void run_wifi_diag_tests(void);

int main(int, char **)
{
    UNITY_BEGIN();
    run_protocol_tests();
    run_queue_tests();
    run_session_tests();
    run_model_tests();
    run_wifi_diag_tests();
    return UNITY_END();
}
