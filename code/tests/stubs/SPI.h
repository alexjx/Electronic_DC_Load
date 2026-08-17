#ifndef ELECTRONIC_DC_LOAD_HOST_SPI_H
#define ELECTRONIC_DC_LOAD_HOST_SPI_H

#include <stdint.h>
#include <vector>

class SPISettings
{
public:
    SPISettings(uint32_t, uint8_t, uint8_t)
    {
    }
};

class SPIClass
{
public:
    std::vector<std::vector<uint8_t> > transfers;
    std::vector<std::vector<uint8_t> > responses;

    void beginTransaction(const SPISettings&)
    {
    }

    void endTransaction()
    {
    }

    void transfer(uint8_t* data, uint8_t count)
    {
        transfers.push_back(std::vector<uint8_t>(data, data + count));
        if (!responses.empty()) {
            const std::vector<uint8_t> response = responses.front();
            responses.erase(responses.begin());
            for (uint8_t index = 0; index < count; ++index) {
                data[index] = index < response.size() ? response[index] : 0;
            }
        } else {
            for (uint8_t index = 0; index < count; ++index) {
                data[index] = 0;
            }
        }
    }
};

extern SPIClass SPI;

#define MSBFIRST 1
#define SPI_MODE3 3

#endif
