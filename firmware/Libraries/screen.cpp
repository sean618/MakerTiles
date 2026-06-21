/* Copyright (c) 2024 Sean Bremner */

#include "project.h"
#ifdef ENABLE_SCREEN_MODULE

// Half Duplex master SPI - 8 bits motorola MSB first
// Add SPI Tx DMA normal byte 
// Enable SPI interrupt 

#include <stdint.h>
#include <string.h>
#include "tft.h"
#include "fonts.h"
#include "useful.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240


// TODO: we have about 2.4KB plus maybe 1KB from stack and heap?
// So make this buffer 4 rows work of data (width * 8 bytes)
// Then write the data into the buffer every dma half complete callback - then screen updates should be fast
#define DMA_BUFFER_SIZE_BYTES (SCREEN_WIDTH*2) // DO NOT CHANGE: it's the size of a row

#define MAX_PACKET_PIXELS 80 // 80 * 2B -> 160B (which fits into the max packet size ~250B)
#define NUM_CHARACTERS 64
// #define COMMAND_BUFFER_LENGTH 32 // 32 bit words

#define DATA_BUFFER_BYTES 2300

// typedef struct {
//     uint8_t command;
//     uint8_t length;
//     uint16_t dataBufferStart;
// } tCommandWord;

// typedef struct {
// 	uint8_t start;
// 	uint8_t end;
// 	uint8_t size;
// 	tCommandWord entries[COMMAND_BUFFER_LENGTH];
// } tCommandBuffer;


typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} tPixelWindow;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t colour;
} tDrawRectangle;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t size;
    uint16_t colour;
    uint16_t backgroundColour;
    uint16_t numChar;
} tDrawText;

typedef struct {
    uint16_t running;
    uint16_t rowPixelIndex;
} tPixelStream;

typedef struct {
    GPIO_TypeDef* backlightGpioSection;
    uint16_t backlightGpioPin;
    uint32_t cmdSpaceRemaining;
    uint8_t backlightEnabled;
    uint8_t invertColours;
    uint8_t rotation;
    uint8_t resolutionScaling;
    uint8_t newResolutionScaling;

    tDrawRectangle drawRectangle;
    char text[NUM_CHARACTERS];
    tDrawText drawText;

    // uint16_t triangleX1;
    // uint16_t triangleY1;
    // uint16_t triangleX2;
    // uint16_t triangleY2;
    // uint16_t triangleX3;
    // uint16_t triangleY3;
    // uint16_t triangleColour;

    tPixelWindow pixelWindow;
    tPixelWindow newPixelWindow;
    tPixelStream pixelStream;

    // Store all incoming commands because they can take a while to complete
    uint8_t dataBufferData[DATA_BUFFER_BYTES];
    sCircularBuffer dataBuffer;

    // tCommandBuffer commandBuffer;

    // DMA buffer - needs to be big enough to fit an entire row
} tScreenBoardDriver;

tScreenBoardDriver screen = {};

// __attribute__ ((section(".dmadata")))
uint8_t screenDmaBuffer[DMA_BUFFER_SIZE_BYTES];

#define SET_PIXEL_WINDOW_COMMAND 0x1
#define START_STREAM_PIXELS_COMMAND 0x2
#define STREAM_PIXELS_COMMAND 0x3
#define DRAW_RECTANGLE_COMMAND 0x4
#define DRAW_TEXT_COMMAND 0x5
#define CHANGE_RESOLUTION_SCALING 0x6
#define STOP_STREAM_PIXELS_COMMAND 0x7

// ============================================ //


// TODO: speed up by calculating before dma finished

void drawText(uint16_t startX, uint16_t startY, char* str, uint16_t numChar, uint8_t textSize, uint16_t textColour, uint16_t backgroundColour) {
    // FontDef font = Font_11x18;
    FontDef font = Font_7x10;

    startX *= screen.resolutionScaling;
    startY *= screen.resolutionScaling;
    if ((startX >= SCREEN_WIDTH) || (startY >= SCREEN_HEIGHT)) {
        myAssert(0, "");
        return;
    }
    if ((font.height * textSize) >= (SCREEN_HEIGHT - startY)) {
        myAssert(0, "");
        return;
    }

    uint16_t littleEndianTextColour = ((textColour & 0xff00) >> 8) | ((textColour & 0xff) << 8);
    uint16_t littleEndianBGColour = ((backgroundColour & 0xff00) >> 8) | ((backgroundColour & 0xff) << 8);

    // Find length of each line
    #define MAX_NUM_LINES 10
    uint16_t lengths[MAX_NUM_LINES] = {};
    uint8_t lines = 0;
    uint16_t maxLength = 0;
    for (uint16_t i=0; i<numChar; i++) {
        if (str[i] == '\0') {
            lines++;
            break;
        }
        if (str[i] == '\n') {
            lines++;
            // End if too many lines
            if (lines >= MAX_NUM_LINES) {
                break;
            }
            if (((lines +1) * font.height * textSize) >= (SCREEN_HEIGHT - startY)) {
                break;
            }
        } else {
            if (((lengths[lines]+1) * textSize * font.width) < (SCREEN_WIDTH - startX)) {
                lengths[lines]++;
                maxLength = MAX(maxLength, lengths[lines]);
            }
            // Ignore character if too long
        }
    }

    uint16_t width = font.width * textSize * maxLength;
    uint16_t height = font.height * textSize * lines;
    myAssert(width < SCREEN_WIDTH, "");
    myAssert(height < SCREEN_HEIGHT, "");
    tftSetAddressWindow(startX, startY, startX + width - 1, startY + height + textSize - 1);

    // First draw a line at the top to form a border
    for (uint16_t z=0; z<width; z++) {
        ((uint16_t *)screenDmaBuffer)[z] = littleEndianBGColour;
    }
    for (uint8_t i=0; i<textSize; i++) {
        tftWriteDataDma(screenDmaBuffer, 2*width);
        waitForDma();
    }

    char * tmpStr = str;
    for (uint8_t l=0; l<lines; l++) {
        // Deal with any line length differences
        uint16_t diff = (maxLength - lengths[l]) * font.width * textSize;

        // Loop through all rows that make up the font
        for(uint32_t y=0; y<font.height; y++) {
            uint16_t * dmaPtr = (uint16_t *) screenDmaBuffer;

            // Loop through all the characters in the line
            for (uint8_t ci=0; ci<lengths[l]; ci++) {
                uint8_t ch = tmpStr[ci];
                uint16_t row = font.data[(ch - 32) * font.height + y];

                // Set a row of the character
                for (uint16_t z=0; z<font.width; z++) {
                    uint16_t colour = (row & 0x8000) ? littleEndianTextColour : littleEndianBGColour;
                    for (uint8_t j=0; j<textSize; j++) {
                        *dmaPtr = colour;
                        dmaPtr++;
                    }
                    row = row << 1;
                }
            }

            // Set line length difference pixels in row
            for (uint16_t i=0; i<diff; i++) {
                *dmaPtr = littleEndianBGColour;
                dmaPtr++;
            }
            myAssert(((uint8_t *) dmaPtr) < (screenDmaBuffer + DMA_BUFFER_SIZE_BYTES), "");

            // Write row (multiples times if the text size is > 1)
            for (uint8_t i=0; i<textSize; i++) {
                tftWriteDataDma(screenDmaBuffer, 2*width);
                waitForDma();
            }
        }
        tmpStr += lengths[l] + 1;
        myAssert(tmpStr <= (str + numChar), "");
    }


    // for (uint16_t z=0; z<width; z++) {
    //     ((uint16_t *)screenDmaBuffer)[z] = littleEndianBGColour;
    // }
    // tftWriteDataDma(screenDmaBuffer, 2*width);
    // waitForDma();


    finishDmaWrite();
}

// ========================================= //

void drawRectangle(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t colour) {
    x *= screen.resolutionScaling;
    y *= screen.resolutionScaling;
    width *= screen.resolutionScaling;
    height *= screen.resolutionScaling;
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
        return;
    width = MIN(width, (SCREEN_WIDTH-x));
    height = MIN(height, (SCREEN_HEIGHT-y));
    if (width == 0 || height == 0)
        return;

    uint16_t littleEndianColour = ((colour & 0xff00) >> 8) | ((colour & 0xff) << 8);

    for (uint16_t p=0; p<width; p++) {
        ((uint16_t *) screenDmaBuffer)[p] = littleEndianColour;
    }
    tftSetAddressWindow(x, y, x+width-1, y+height-1);
    for (uint16_t i=0; i<height; i++) {
        tftWriteDataDma(screenDmaBuffer, 2*width);
        waitForDma();
    }
    waitForDma();
    finishDmaWrite();
}

// ========================================= //

void finishPixelStreaming() {
    finishDmaWrite();
    memset(&screen.pixelStream, 0, sizeof(screen.pixelStream));
}

void startPixelStreaming() {
    myAssert(!screen.pixelStream.running, "");
    screen.pixelStream.running = 1;
    if (screen.pixelWindow.width == 0 || screen.pixelWindow.height == 0)
    	return;
    uint8_t scaling = screen.resolutionScaling;
    uint16_t startX = screen.pixelWindow.x * scaling;
    uint16_t startY = screen.pixelWindow.y * scaling;
    uint16_t endX = startX + (screen.pixelWindow.width  * scaling) - 1;
    uint16_t endY = startY + (screen.pixelWindow.height * scaling) - 1;
    // These asserts should never occur because the checks should be done before the command goes into the buffer
    myAssert(startX < SCREEN_WIDTH, "Invalid pixel stream start x");
    myAssert(startY < SCREEN_HEIGHT, "Invalid pixel stream start y");
    myAssert(endX < SCREEN_WIDTH, "Invalid pixel stream end x");
    myAssert(endY < SCREEN_HEIGHT, "Invalid pixel stream end y");
    tftSetAddressWindow(startX, startY, endX, endY);
}

void streamPixels(uint16_t numNewPixels) {
    uint16_t scaling = screen.resolutionScaling;
    tPixelWindow * w = &screen.pixelWindow;
    tPixelStream * stream = &screen.pixelStream;

    // Discard any pixels if not actually running
    if (!screen.pixelStream.running || w->width == 0 || w->height == 0) {
        popBuffer(&screen.dataBuffer, &screenDmaBuffer[0], numNewPixels*2);
        return;
    }

    while (numNewPixels) {
        // Work out how many pixels to put into the DMA buffer
        uint16_t dmaPixelsLeft = w->width - stream->rowPixelIndex;
        uint16_t numToCopy = MIN(dmaPixelsLeft, numNewPixels);
        numNewPixels -= numToCopy;

        uint8_t * dma = &screenDmaBuffer[stream->rowPixelIndex * scaling * 2];

        // Need to duplicate each pixel to make row
        myAssert(numToCopy <= SCREEN_WIDTH/2, "");
        uint16_t tmpPixels[SCREEN_WIDTH/2]; // TODO: quite a lot for the stack
        popBuffer(&screen.dataBuffer, (uint8_t *) tmpPixels, numToCopy*2);
        uint16_t * pixels = tmpPixels;
        
        if (scaling == 1) {
            for (uint16_t p=0; p<numToCopy; p++) {
                uint16_t pixelColour = (*pixels) >> 8 | (*pixels) << 8;
                *((uint16_t *) dma) = pixelColour;
                dma += 2;
                pixels++;
            }

        } else {
            for (uint16_t p=0; p<numToCopy; p++) {
                uint16_t pixelColour = (*pixels) >> 8 | (*pixels) << 8;
                for (uint8_t r=0; r<scaling; r++) {
                    *((uint16_t *) dma) = pixelColour;
                    dma += 2;
                }
                pixels++;
            }
        }
        stream->rowPixelIndex += numToCopy;
        // Write the row
        if (stream->rowPixelIndex == w->width) {
            stream->rowPixelIndex = 0;
            waitForDma();
            for (uint8_t i=0; i<scaling; i++) {
                tftWriteDataDma(screenDmaBuffer, 2 * w->width * scaling);
                waitForDma();
            }
        }
    }
}

// ========================================= //

// Running in background thread so it doesn't matter that we are waiting on the dma all the time
void processActions() {
    if (bufferLength(&screen.dataBuffer) == 0) {
        return;
    }
    myAssert(!dmaBusy(), "Always wait for DMA to finish");
    while (dmaBusy()) {
    	;
    }

    uint8_t header[2] = {};
    popBuffer(&screen.dataBuffer, header, 2);
    uint8_t command = header[0];
    uint8_t length = header[1];

    // If non streaming command comes in whilst still streaming - kill the stream
    if ((command != STREAM_PIXELS_COMMAND && command != STOP_STREAM_PIXELS_COMMAND) && screen.pixelStream.running) {
        //myAssert(0, "Pixel streaming interrupted");
        finishPixelStreaming();
    }

    // Get new actions
    if (command == SET_PIXEL_WINDOW_COMMAND) {
        myAssert(length == sizeof(screen.pixelWindow), "length doesn't match");
        popBuffer(&screen.dataBuffer, (uint8_t *) &screen.pixelWindow, length);

    } else if (command == START_STREAM_PIXELS_COMMAND) {
        startPixelStreaming();

    } else if (command == STOP_STREAM_PIXELS_COMMAND) {
        finishPixelStreaming();

    } else if (command == STREAM_PIXELS_COMMAND) {
        myAssert(screen.pixelStream.running, "Pixel streaming not started");
        streamPixels(length/2);

    } else if (command == DRAW_RECTANGLE_COMMAND) {
        // HAL_Delay(10000);
        tDrawRectangle action;
        myAssert(length == sizeof(tDrawRectangle), "length doesn't match");
        popBuffer(&screen.dataBuffer, (uint8_t *) &action, length);
        drawRectangle(action.x, action.y, action.width, action.height, action.colour);

    } else if (command == DRAW_TEXT_COMMAND) {
        tDrawText action;
        char text[NUM_CHARACTERS];
        popBuffer(&screen.dataBuffer, (uint8_t *) &action, sizeof(tDrawText));
        myAssert(length == sizeof(tDrawText) + action.numChar, "");
        myAssert(action.numChar < NUM_CHARACTERS, "String too long");
        popBuffer(&screen.dataBuffer, (uint8_t *) text, action.numChar);
        drawText(action.x, action.y, text, action.numChar, action.size, action.colour, action.backgroundColour);

    } else if (command == CHANGE_RESOLUTION_SCALING) {
        myAssert(length == sizeof(screen.resolutionScaling), "length doesn't match");
        popBuffer(&screen.dataBuffer, (uint8_t *) &screen.resolutionScaling, length);

    } else {
        myAssert(0, "Invalid command");
    }
}

// ========================================= //


// void refreshAndPublishCmdSpaceRemaining() {
//     screen.cmdSpaceRemaining = DATA_BUFFER_BYTES - bufferLength(&screen.dataBuffer);
//     field_t fieldIndex = SCREEN_FIELDS_OFFSET + 15 + NUM_CHARACTERS;
//     publishFieldsIfBelowBandwidth(fieldIndex, fieldIndex);
// }

bool storeCommand(uint8_t command, uint8_t * data, uint8_t dataLength) {
    // screen.cmdSpaceRemaining = DATA_BUFFER_BYTES - bufferLength(&screen.dataBuffer);
    // if (screen.cmdSpaceRemaining > dataLength + 2) {
        uint8_t header[2] = {command, dataLength};
        uint16_t tmpEnd = screen.dataBuffer.end;
        if (appendBufferWithTmpEnd(&screen.dataBuffer, &tmpEnd, header, 2) < 0) {
            return false;
        }
        if (dataLength > 0) {
            if (appendBufferWithTmpEnd(&screen.dataBuffer, &tmpEnd, data, dataLength) < 0) {
                return false;
            }
        }
        screen.dataBuffer.end = tmpEnd;
        screen.cmdSpaceRemaining = DATA_BUFFER_BYTES - bufferLength(&screen.dataBuffer);
        return true;
    // }
    // return false;
}

// ========================================= //

bool setResolutionScaling(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    if (*data > 0 && *data <= SCREEN_HEIGHT) {
        screen.newResolutionScaling = *data;
        return storeCommand(CHANGE_RESOLUTION_SCALING, &screen.newResolutionScaling, sizeof(screen.newResolutionScaling));
    }
    return true;
}

bool setDrawRectangle(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    return storeCommand(DRAW_RECTANGLE_COMMAND, (uint8_t *) &screen.drawRectangle, sizeof(screen.drawRectangle));
}

bool setDrawText(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    screen.text[NUM_CHARACTERS-1] = '\0'; // Make sure it always ends in terminating char
    screen.drawText.numChar = strlen(screen.text)+1;

    uint8_t datasize = sizeof(tDrawText) + screen.drawText.numChar;
    // if (screen.cmdSpaceRemaining > datasize + 2) {
        uint8_t header[2] = {DRAW_TEXT_COMMAND, datasize};
        uint16_t tmpEnd = screen.dataBuffer.end;
        if (appendBufferWithTmpEnd(&screen.dataBuffer, &tmpEnd, header, 2) < 0) {
            return false;
        }
        if (appendBufferWithTmpEnd(&screen.dataBuffer, &tmpEnd, (uint8_t *) &screen.drawText, sizeof(tDrawText)) < 0) {
            return false;
        }
        if (appendBufferWithTmpEnd(&screen.dataBuffer, &tmpEnd, (uint8_t *) screen.text, screen.drawText.numChar) < 0) {
            return false;
        }
        screen.dataBuffer.end = tmpEnd;
        return true;
    // }
}

uint16_t pixStartCount = 0;
uint16_t pixFinishedCount = 0;
uint16_t pixStreamCount = 0;

bool setStopPixelStreaming(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    pixFinishedCount++;
    return storeCommand(STOP_STREAM_PIXELS_COMMAND, 0, 0);
}

bool setStartPixelStreaming(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    pixStartCount++;
    // Check if window is valid
    tPixelWindow * w = &screen.newPixelWindow;
    uint16_t scaling = screen.newResolutionScaling;
    uint32_t endX = (w->x + w->width) * scaling;
    uint32_t endY = (w->y + w->height) * scaling;
    if ((endX > SCREEN_WIDTH) || (endY > SCREEN_HEIGHT)) {
        w->x = 0;
        w->y = 0;
        w->width = 0;
        w->height = 0;
    }
    // Store a set window command if the window parameters have changed then
    static tPixelWindow lastWindow = {};
    if ((screen.newPixelWindow.x      != lastWindow.x) ||
        (screen.newPixelWindow.y      != lastWindow.y) ||
        (screen.newPixelWindow.width  != lastWindow.width) ||
        (screen.newPixelWindow.height != lastWindow.height)) {
        if (storeCommand(SET_PIXEL_WINDOW_COMMAND, (uint8_t *) w, sizeof(tPixelWindow)) == false) {
            return false;
        }
        lastWindow = *w;
    }
    return storeCommand(START_STREAM_PIXELS_COMMAND, 0, 0);
}

bool setPixelDataStream(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    pixStreamCount++;
    uint16_t numPixels = numFields;
    return storeCommand(STREAM_PIXELS_COMMAND, data, numPixels * 2);
}

bool setTextSize(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    if (*data > 0) {
        screen.drawText.size = *data;
    }
    return true;
}

bool setInvertColours(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    tftInvertColors(!(data[0]));
    return true;
}

bool setRotation(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
    tftSetRotation(data[0]);
    return true;
}
// void setBacklightField(fp::FieldEntry * field, fp::FieldIndex fieldOffset, fp::FieldIndex numFields, uint8_t * data) {
//     setBacklight(data[0]);
// }

const fp::FieldEntry screenFields[] = {
    { &screen.drawRectangle.x,               "rectangle_x",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawRectangle.y,               "rectangle_y",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawRectangle.width,           "rectangle_width",          1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawRectangle.height,          "rectangle_height",         1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawRectangle.colour,          "rectangle_colour",         1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { NULL,                                  "rectangle_draw",           1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setDrawRectangle,       NULL, NULL },
    { &screen.text[0],                       "text",                     NUM_CHARACTERS,        fp::FieldDataType::Utf8Char, 1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawText.x,                    "text_x",                   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawText.y,                    "text_y",                   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawText.size,                 "text_size",                1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setTextSize,            NULL, NULL },
    { &screen.drawText.colour,               "text_colour",              1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.drawText.backgroundColour,     "text_background_colour",   1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { NULL,                                  "text_draw",                1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setDrawText,            NULL, NULL },
    { &screen.newResolutionScaling,          "resolution_scaling",       1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setResolutionScaling,   NULL, NULL },
    { &screen.invertColours,                 "invert_colours",           1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setInvertColours,       NULL, NULL },
    { &screen.rotation,                      "rotation",                 1,                     fp::FieldDataType::Uint,      1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setRotation,            NULL, NULL },
    { &screen.cmdSpaceRemaining,             "cmd_space_remaining",      1,                     fp::FieldDataType::Uint,      4,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.newPixelWindow.x,              "pixel_window_x",           1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.newPixelWindow.y,              "pixel_window_y",           1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.newPixelWindow.width,          "pixel_window_width",       1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { &screen.newPixelWindow.height,         "pixel_window_height",      1,                     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, NULL,                    NULL, NULL },
    { NULL,                                  "start_pixel_streaming",    1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setStartPixelStreaming, NULL, NULL },
    { NULL,                                  "pixel_data_stream",        MAX_PACKET_PIXELS,     fp::FieldDataType::Uint,      2,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setPixelDataStream,     NULL, NULL },
    { NULL,                                  "stop_pixel_streaming",     1,                     fp::FieldDataType::Boolean,   1,   fp::FieldFlags::Gettable | fp::FieldFlags::Settable, &setStopPixelStreaming,  NULL, NULL },
};
const fp::FieldTable screenFieldTable = {
    .fields = (fp::FieldEntry*) screenFields,
    .numFields = sizeof(screenFields)/sizeof(fp::FieldEntry)
};

void setBacklight(uint8_t enable) {
    HAL_GPIO_WritePin(screen.backlightGpioSection, screen.backlightGpioPin, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void initialiseScreen(GPIO_TypeDef* powerGpioSection,
                    uint16_t powerGpioPin, 
                    GPIO_TypeDef* csGpioSection,
                    uint16_t csGpioPin,
                    GPIO_TypeDef* dcGpioSection,
                    uint16_t dcGpioPin,
                    GPIO_TypeDef* resetGpioSection,
                    uint16_t resetGpioPin,
                    GPIO_TypeDef* backlightGpioSection,
                    uint16_t backlightGpioPin, 
                    SPI_HandleTypeDef * spi) {
    memset(screenDmaBuffer, 0, sizeof(screenDmaBuffer));
    screen.backlightGpioSection = backlightGpioSection;
    screen.backlightGpioPin = backlightGpioPin;
    screen.backlightEnabled = 1;
    screen.resolutionScaling = 1;
    screen.newResolutionScaling = 1;
    screen.dataBuffer.data = screen.dataBufferData;
    screen.dataBuffer.size = sizeof(screen.dataBufferData);
    screen.cmdSpaceRemaining = DATA_BUFFER_BYTES;
    
    HAL_GPIO_WritePin(resetGpioSection, resetGpioPin, GPIO_PIN_RESET);
    
    if (powerGpioSection != NULL) {
        HAL_GPIO_WritePin(powerGpioSection, powerGpioPin, GPIO_PIN_SET);
        HAL_Delay(1000);
        HAL_GPIO_WritePin(powerGpioSection, powerGpioPin, GPIO_PIN_RESET);
        // HAL_Delay(50);
    }
    
    HAL_Delay(100);

    setBacklight(screen.backlightEnabled);

    initTft(csGpioSection, csGpioPin, dcGpioSection, dcGpioPin,
            resetGpioSection, resetGpioPin, spi);

    #if IS_ST7789
        tftInvertColors(1);
    #else
        tftInvertColors(0);
    #endif
     
    // tftSetRotation(3);

    // uint32_t start = HAL_GetTick();
    // for (uint32_t i=0; i<1000; i++) {
    //     drawRectangle(0,i%200, 320, 1, 0xFFFF);
    // }
    // volatile uint32_t diff = HAL_GetTick() - start;

    {
        tDrawRectangle action = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK };
        screen.drawRectangle = action;
        setDrawRectangle(NULL, 0, 0, NULL);
    }

    // {
    //     tDrawRectangle action = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLUE };
    //     screen.drawRectangle = action;
    //     setDrawRectangle(NULL, 0, 0, NULL);
    // }

//     {
//         char text[] = "Start";
// //        tDrawText action = { 160-(11*13*2/2), 120-(18*2/2), 1, BLACK, WHITE, 0};
//         tDrawText action = { 10, 10, 2, BLACK, WHITE, 0};
//         screen.drawText = action;
//         action.numChar = sizeof(text);
//         memcpy(screen.text, text, action.numChar);
//         setDrawText(NULL, 0, 0, NULL);
//     }
    // {
    //     char text[] = "Starting...";
    //     tDrawText action = { 0,200, 1, BLACK, WHITE, 0};
    //     screen.drawText = action;
    //     action.numChar = sizeof(text);
    //     memcpy(screen.text, text, action.numChar);
    //     setDrawText(NULL, 0, 0, NULL);
    // }
    // {
    //     char text[] = "Starting...";
    //     tDrawText action = { 0, 0, 4, BLACK, WHITE, 0};
    //     screen.drawText = action;
    //     action.numChar = sizeof(text);
    //     memcpy(screen.text, text, action.numChar);
    //     setDrawText(NULL, 0, 0, NULL);
    // }
}

// void afterCanSetupScreen() {
//     {
//         tDrawRectangle action = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BLACK };
//         screen.drawRectangle = action;
//         setDrawRectangle(NULL, 0, 0, NULL);
//     }

//     {
//         char text[] = "Welcome";
//         tDrawText action = { 160-(11*7*2/2), 120-(18*2/2), 2, WHITE, BLACK, 0};
//         screen.drawText = action;
//         action.numChar = sizeof(text);
//         memcpy(screen.text, text, action.numChar);
//         setDrawText(NULL, 0, 0, NULL);
//     }
// }

void loopScreen(uint32_t ticks) {
    // static uint32_t nextTick = 0;
    // static uint32_t idx = 0;
    // if (ticks > nextTick) {
    //     nextTick = ticks + 4000;
    //     idx++;
    //     idx = idx % 2;

    //     tDrawRectangle action = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, idx ? BLUE : WHITE };
    //     screen.drawRectangle = action;
    //     setDrawRectangle(NULL, 0, 0, NULL);
    // }
    
    processActions();
    screen.cmdSpaceRemaining = DATA_BUFFER_BYTES - bufferLength(&screen.dataBuffer);
    
    // static uint32_t lastTime = 0;
    
    
    // if (oldSpaceremaining != screen.cmdSpaceRemaining && (ticks > lastTime + 20)) { // Publish at maximum 50Hz
    //     lastTime = ticks;
    //     field_t fieldIndex = SCREEN_FIELDS_OFFSET + 15 + NUM_CHARACTERS;
    //     publishFieldsIfBelowBandwidth(fieldIndex, fieldIndex);
    // }
    
	// static uint32_t last5secondTime = 0;
	// if ((ticks - last5secondTime) > 5000) {
	// 	last5secondTime = ticks;
	// 	publishFieldsIfBelowBandwidth(SCREEN_FIELDS_OFFSET, SCREEN_FIELDS_OFFSET +  4);
	// 	publishFieldsIfBelowBandwidth(SCREEN_FIELDS_OFFSET +  6 + NUM_CHARACTERS, SCREEN_FIELDS_OFFSET + 19 + NUM_CHARACTERS);
	// }
}

// void changeScreenSpiFreq() {
//     SPI_TypeDef * instance = tftDrv.spi->Instance;
//     SPI_InitTypeDef init = tftDrv.spi->Init;
//     myAssert(HAL_SPI_DeInit(tftDrv.spi) == HAL_OK, "");
//     tftDrv.spi->Instance = instance;
//     tftDrv.spi->Init = init;
//     // Change the SPI speed from 24 MHz to 3Mhz
//     tftDrv.spi->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
//     myAssert(HAL_SPI_Init(tftDrv.spi) == HAL_OK, "");
//     initialiseScreen(
//         tftDrv.csGpioSection,
//         tftDrv.csGpioPin,
//         tftDrv.dcGpioSection,
//         tftDrv.dcGpioPin,
//         tftDrv.resetGpioSection,
//         tftDrv.resetGpioPin,
//         screen.backlightGpioSection,
//         screen.backlightGpioPin,
//         tftDrv.spi
//     );
// }


#endif //ENABLE_SCREEN_MODULE


// void testPixelStreaming() {
//     uint8_t tmp3 = 30;
//     setResolutionScaling(NULL, 0, 0, &tmp3);
    
    

//     // All good test
//     {
//         tPixelWindow tmp = {0,0,6,2};
//         screen.newPixelWindow = tmp;
//         setStartPixelStreaming(NULL, 0, 0, NULL);

//         uint16_t data[] = {
//             RED, YELLOW, RED, YELLOW, RED, YELLOW,
//             YELLOW, RED, YELLOW, RED, YELLOW, RED,
//         };
//         setPixelDataStream(NULL, 0, 2*6, (uint8_t *) data);
//         setStopPixelStreaming(NULL, 0, 0, NULL);
//     }

//     // Too little data
//     {
//         tPixelWindow tmp = {0,2,6,2};
//         screen.newPixelWindow = tmp;
//         setStartPixelStreaming(NULL, 0, 0, NULL);

//         uint16_t data[] = {
//             RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
//             RED, YELLOW, GREEN, 
//         };
//         setPixelDataStream(NULL, 0, 2*6, (uint8_t *) data);
//         setStopPixelStreaming(NULL, 0, 0, NULL);
//     }

//     // Too much data
//     {
//         tPixelWindow tmp = {0,4,6,2};
//         screen.newPixelWindow = tmp;
//         setStartPixelStreaming(NULL, 0, 0, NULL);

//         uint16_t data[] = {
//             RED, YELLOW, GREEN, CYAN, BLUE, MAGENTA,
//             BLACK, YELLOW, BLACK, CYAN, BLACK, MAGENTA,
//             WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
//             WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
//             WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
//             WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
//         };
//         setPixelDataStream(NULL, 0, 36, (uint8_t *) data);
//         setStopPixelStreaming(NULL, 0, 0, NULL);
//     }

//     // Window too big
//     {
//         tPixelWindow tmp = {0,4,20,2};
//         screen.newPixelWindow = tmp;
//         setStartPixelStreaming(NULL, 0, 0, NULL);

//         uint16_t data[] = {
//             GREY, GREY, GREY, GREY, GREY, GREY,
//             GREY, GREY, GREY, GREY, GREY, GREY,
//             GREY, GREY, GREY, GREY, GREY, GREY,
//             GREY, GREY, GREY, GREY, GREY, GREY,
//             GREY, GREY, GREY, GREY, GREY, GREY,
//             GREY, GREY, GREY, GREY, GREY, GREY,
//         };
//         setPixelDataStream(NULL, 0, 36, (uint8_t *) data);
//         setStopPixelStreaming(NULL, 0, 0, NULL);
//     }


//     uint8_t tmp2 = 1;
//     setResolutionScaling(NULL, 0, 0, &tmp2);
    
//     while (bufferLength(&screen.dataBuffer)) {
//         processActions();
//     }
    
    

//     // Image
//     {
//         tPixelWindow tmp = {0,0,320,240};
//         screen.newPixelWindow = tmp;
//         setStartPixelStreaming(NULL, 0, 0, NULL);
        
//         uint32_t i=0;
//         uint16_t data[80];
//         for (uint32_t y=0; y<240; y++) {
//             for (uint32_t x=0; x<320; x++) {
//                 uint16_t colour = (((y + x)/2) % 64) << 5;
//                 data[i] = colour;
//                 i++;
//                 if (i == 80) {
//                 	i=0;
//                     setPixelDataStream(NULL, 0, 80, (uint8_t *) data);
//                     while (bufferLength(&screen.dataBuffer)) {
//                         processActions();
//                     }
//                 }
//             }
//         }
//         setStopPixelStreaming(NULL, 0, 0, NULL);
//     }
// }
