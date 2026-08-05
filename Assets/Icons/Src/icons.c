#include "icons.h"

/* The ASCII art above each table is the source of truth - edit that first,
 * then the bytes. '#' is a set pixel. */

/*  .....##
 *  ....###
 *  ....##.
 *  ....##.
 *  ....##.
 *  ....##.
 *  .####..
 *  ######.
 *  .####..
 */
const uint8_t icon_note[9] = {
    0x06, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x78, 0xFC, 0x78,
};

/*  ...######...
 *  ..##....##..
 *  .##......##.
 *  ##........##
 *  ##........##
 *  ###......###
 *  ###......###
 *  .#........#.
 */
const uint8_t icon_headphones[16] = {
    0x1F, 0x80, 0x30, 0xC0, 0x60, 0x60, 0xC0, 0x30,
    0xC0, 0x30, 0xE0, 0x70, 0xE0, 0x70, 0x40, 0x20,
};

/*  ...#......
 *  ..##...#..
 *  .###.#..#.
 *  ####.#.#.#
 *  ####.#.#.#
 *  ####.#.#.#
 *  .###.#..#.
 *  ..##...#..
 *  ...#......
 */
const uint8_t icon_speaker[18] = {
    0x10, 0x00, 0x31, 0x00, 0x74, 0x80, 0xF5, 0x40, 0xF5,
    0x40, 0xF5, 0x40, 0x74, 0x80, 0x31, 0x00, 0x10, 0x00,
};
