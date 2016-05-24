#include <iostream>
#include <freesrp.hpp>
#include <cstring>
#include <vector>
#include <chrono>
#include <readerwriterqueue/readerwriterqueue.h>

#include <emmintrin.h>

using namespace std;

moodycamel::ReaderWriterQueue<FreeSRP::sample> _rx_buf(1024 * 64 * 128);

void rx_original(unsigned char *buffer, int buffer_length)
{
    for(int i = 0; i < buffer_length; i+=4)
    {
        uint16_t raw_i;
        uint16_t raw_q;
        memcpy(&raw_q, buffer + i, sizeof(raw_q));
        memcpy(&raw_i, buffer + i + sizeof(raw_q), sizeof(raw_i));

        // Convert the raw I/Q values from 12-bit (two's complement) to 16-bit signed integers
        int16_t signed_i;
        int16_t signed_q;
        // Do sign extension
        bool i_negative = (raw_i & (1 << 11)) != 0;
        bool q_negative = (raw_q & (1 << 11)) != 0;
        if(i_negative)
        {
            signed_i = (int16_t) (raw_i | ~((1 << 11) - 1));
        }
        else
        {
            signed_i = raw_i;
        }
        if(q_negative)
        {
            signed_q = (int16_t) (raw_q | ~((1 << 11) - 1));
        }
        else
        {
            signed_q = raw_q;
        }

        // Convert the signed integers (range -2048 to 2047) to floats (range -1 to 1)
        FreeSRP::sample s;
        s.i = (float) signed_i / 2048.0f;
        s.q = (float) signed_q / 2048.0f;

        bool success = _rx_buf.try_enqueue(s);
        if(!success)
        {
            // TODO: overflow! handle this
        }
    }
}

void rx_new1(unsigned char *buffer, int buffer_length)
{
    for(int i = 0; i < buffer_length; i+=4*8)
    {
        uint16_t raw_q[8] = {*((uint16_t *) (buffer + i)),
                             *((uint16_t *) (buffer + i + 4)),
                             *((uint16_t *) (buffer + i + 4*2)),
                             *((uint16_t *) (buffer + i + 4*3)),
                             *((uint16_t *) (buffer + i + 4*4)),
                             *((uint16_t *) (buffer + i + 4*5)),
                             *((uint16_t *) (buffer + i + 4*6)),
                             *((uint16_t *) (buffer + i + 4*7))};

        uint16_t raw_i[8] = {*((uint16_t *) (buffer + i       + 2)),
                             *((uint16_t *) (buffer + i + 4   + 2)),
                             *((uint16_t *) (buffer + i + 4*2 + 2)),
                             *((uint16_t *) (buffer + i + 4*3 + 2)),
                             *((uint16_t *) (buffer + i + 4*4 + 2)),
                             *((uint16_t *) (buffer + i + 4*5 + 2)),
                             *((uint16_t *) (buffer + i + 4*6 + 2)),
                             *((uint16_t *) (buffer + i + 4*7 + 2))};

        __m128i *raw_q_8 = (__m128i *) raw_q;
        __m128i *raw_i_8 = (__m128i *) raw_i;

        __m128i signed_q_8;
        __m128i signed_i_8;

        _mm_store_si128(&signed_q_8, _mm_or_si128(*raw_q_8, _mm_set1_epi16(~((1 << 11) - 1))));
        _mm_store_si128(&signed_i_8, _mm_or_si128(*raw_i_8, _mm_set1_epi16(~((1 << 11) - 1))));

        // Convert to float
        __m128i q_32_lo = _mm_unpacklo_epi16(signed_q_8, _mm_set1_epi16(0));
        __m128i q_32_hi = _mm_unpackhi_epi16(signed_q_8, _mm_set1_epi16(0));
        __m128 q_float_lo = _mm_cvtepi32_ps(q_32_lo);
        __m128 q_float_hi = _mm_cvtepi32_ps(q_32_hi);

        __m128i i_32_lo = _mm_unpacklo_epi16(signed_i_8, _mm_set1_epi16(0));
        __m128i i_32_hi = _mm_unpackhi_epi16(signed_i_8, _mm_set1_epi16(0));
        __m128 i_float_lo = _mm_cvtepi32_ps(i_32_lo);
        __m128 i_float_hi = _mm_cvtepi32_ps(i_32_hi);

        // Normalize, convert the signed integers (range -2048 to 2047) to floats (range -1 to 1)
        q_float_hi = _mm_div_ps(q_float_hi, _mm_set1_ps(2048.0f));
        q_float_lo = _mm_div_ps(q_float_lo, _mm_set1_ps(2048.0f));

        i_float_hi = _mm_div_ps(i_float_hi, _mm_set1_ps(2048.0f));
        i_float_lo = _mm_div_ps(i_float_lo, _mm_set1_ps(2048.0f));

        float signed_q[8];
        float signed_i[8];

        _mm_store_ps(signed_q, q_float_hi);
        _mm_store_ps(signed_q + 4, q_float_lo);

        _mm_store_ps(signed_i, i_float_hi);
        _mm_store_ps(signed_i + 4, i_float_lo);

        // Convert the signed integers (range -2048 to 2047) to floats (range -1 to 1)
        for(int j = 0; j < 8; j++)
        {
            FreeSRP::sample s;
            s.i = signed_i[j];
            s.q = signed_q[j];

            bool success = _rx_buf.try_enqueue(s);
            if (!success)
            {
                // TODO: overflow! handle this
            }
        }
    }
}

int main()
{
    vector<unsigned char> rx_buffer(1024 * 64);

    chrono::high_resolution_clock::time_point t1 = chrono::high_resolution_clock::now();
    for(int i = 0; i < 50; i++)
    {
        rx_original(rx_buffer.data(), (int) rx_buffer.size());
    }
    chrono::high_resolution_clock::time_point t2 = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>( t2 - t1 ).count();
    cout << "rx_original: " << duration/50.0 << "us" << endl;

    t1 = chrono::high_resolution_clock::now();
    for(int i = 0; i < 50; i++)
    {
        rx_new1(rx_buffer.data(), (int) rx_buffer.size());
    }
    t2 = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>( t2 - t1 ).count();
    cout << "rx_new1: " << duration/50.0 << "us" << endl;

    return 0;
}