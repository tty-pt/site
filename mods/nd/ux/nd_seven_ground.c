#include "nd_seven_ground.h"
#include "nd_seven_atlas_draw.h"
#include "nd_seven_motion.h"
#include <stdlib.h>
#include <string.h>
#include <nd/terrain.h>

/* Generated color and optical LUTs for NeverDark Seven water */
__attribute__((unused)) static const uint16_t s_srgb_to_lin[256] = {
	0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219, 241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562, 599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101, 1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863, 1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129, 4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500, 7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090, 12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001, 18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325, 25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143, 34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534, 45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567, 57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535
};

__attribute__((unused)) static const uint8_t s_lin_to_srgb[4096] = {
	0, 1, 2, 2, 3, 4, 5, 6, 6, 7, 8, 9, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23, 23, 24, 24, 25, 25, 25, 26, 26, 27, 27, 27, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 33, 33, 34, 34, 34, 34, 35, 35, 35, 36, 36, 36, 37, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 48, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58, 58, 59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61, 61, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 64, 64, 64, 64, 64, 64, 64, 65, 65, 65, 65, 65, 65, 66, 66, 66, 66, 66, 66, 66, 67, 67, 67, 67, 67, 67, 67, 68, 68, 68, 68, 68, 68, 68, 69, 69, 69, 69, 69, 69, 69, 70, 70, 70, 70, 70, 70, 70, 71, 71, 71, 71, 71, 71, 71, 72, 72, 72, 72, 72, 72, 72, 72, 73, 73, 73, 73, 73, 73, 73, 74, 74, 74, 74, 74, 74, 74, 74, 75, 75, 75, 75, 75, 75, 75, 75, 76, 76, 76, 76, 76, 76, 76, 77, 77, 77, 77, 77, 77, 77, 77, 78, 78, 78, 78, 78, 78, 78, 78, 78, 79, 79, 79, 79, 79, 79, 79, 79, 80, 80, 80, 80, 80, 80, 80, 80, 81, 81, 81, 81, 81, 81, 81, 81, 81, 82, 82, 82, 82, 82, 82, 82, 82, 83, 83, 83, 83, 83, 83, 83, 83, 83, 84, 84, 84, 84, 84, 84, 84, 84, 84, 85, 85, 85, 85, 85, 85, 85, 85, 85, 86, 86, 86, 86, 86, 86, 86, 86, 86, 87, 87, 87, 87, 87, 87, 87, 87, 87, 88, 88, 88, 88, 88, 88, 88, 88, 88, 88, 89, 89, 89, 89, 89, 89, 89, 89, 89, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 91, 91, 91, 91, 91, 91, 91, 91, 91, 91, 92, 92, 92, 92, 92, 92, 92, 92, 92, 92, 93, 93, 93, 93, 93, 93, 93, 93, 93, 93, 94, 94, 94, 94, 94, 94, 94, 94, 94, 94, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 96, 97, 97, 97, 97, 97, 97, 97, 97, 97, 97, 98, 98, 98, 98, 98, 98, 98, 98, 98, 98, 98, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 101, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 105, 106, 106, 106, 106, 106, 106, 106, 106, 106, 106, 106, 106, 107, 107, 107, 107, 107, 107, 107, 107, 107, 107, 107, 107, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 108, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 109, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 111, 111, 111, 111, 111, 111, 111, 111, 111, 111, 111, 111, 111, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 113, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 114, 115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 115, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 116, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 119, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 121, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 122, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 123, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 124, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 125, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 129, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 130, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 131, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 132, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 133, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 134, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 135, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 136, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 137, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 138, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 140, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 141, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 142, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 143, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 144, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 145, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 146, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 147, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 148, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 150, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 151, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 152, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 153, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 154, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 155, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 156, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 157, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 158, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 159, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 160, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 161, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 162, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 163, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 164, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 165, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 166, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 167, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 168, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 169, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 170, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 172, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 173, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 174, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 175, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 176, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 177, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 178, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 179, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 180, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 182, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 183, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 184, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 185, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 186, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 187, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 188, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 189, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 191, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 193, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 194, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 195, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 196, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 197, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 198, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 199, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 200, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 201, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 202, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 205, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 206, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 207, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 208, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 209, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 210, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 211, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 212, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 214, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 215, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 216, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 217, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 218, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 219, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 220, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 221, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 222, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 223, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 225, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 226, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 227, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 228, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 229, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 230, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 231, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 232, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 233, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 234, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 236, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 237, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 238, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 239, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 241, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 242, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 243, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 244, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 246, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 247, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 248, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 251, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 252, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 253, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

__attribute__((unused)) static const uint16_t s_trans_r[29] = {
	256, 199, 155, 120, 94, 73, 57, 44, 34, 26, 21, 16, 12, 9, 7, 6, 4, 3, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0
};
__attribute__((unused)) static const uint16_t s_trans_g[29] = {
	256, 236, 218, 201, 185, 171, 158, 146, 134, 124, 115, 106, 98, 90, 83, 77, 71, 65, 60, 55, 51, 47, 44, 40, 37, 34, 31, 29, 27
};
__attribute__((unused)) static const uint16_t s_trans_b[29] = {
	256, 248, 241, 233, 227, 220, 213, 207, 201, 195, 189, 184, 178, 173, 168, 163, 158, 153, 149, 144, 140, 136, 132, 128, 124, 120, 117, 113, 110
};

__attribute__((unused)) static const uint8_t s_scatter_r[29] = {
	0, 1, 3, 4, 5, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11
};
__attribute__((unused)) static const uint8_t s_scatter_g[29] = {
	0, 5, 9, 14, 18, 21, 24, 27, 30, 32, 34, 36, 38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 48, 49, 50, 50, 50, 51, 51
};
__attribute__((unused)) static const uint8_t s_scatter_b[29] = {
	0, 6, 12, 18, 23, 28, 33, 37, 41, 45, 49, 53, 56, 59, 62, 65, 67, 70, 72, 74, 76, 78, 80, 82, 83, 85, 86, 88, 89
};


static inline uint32_t unit_noise(uint32_t h, int ux, int uy)
{
	return ndw_mix(h ^ (uint32_t)(uy * 16 + ux + 1) * 0x9E3779B1u);
}

static uint8_t *s_active_chunk_depth = NULL;
static uint8_t s_active_chunk_land_mask = 0;
static uint8_t s_active_chunk_grp[9];

static inline uint32_t alpha_blend_rgb(uint32_t bg, uint32_t fg, int alpha)
{
	if (alpha <= 0) return bg;
	if (alpha >= 255) return fg;
	int r_bg = (int)(bg & 0xFF), g_bg = (int)((bg >> 8) & 0xFF), b_bg = (int)((bg >> 16) & 0xFF);
	int r_fg = (int)(fg & 0xFF), g_fg = (int)((fg >> 8) & 0xFF), b_fg = (int)((fg >> 16) & 0xFF);
	int r = r_bg + ((r_fg - r_bg) * alpha) / 255;
	int g = g_bg + ((g_fg - g_bg) * alpha) / 255;
	int b = b_bg + ((b_fg - b_bg) * alpha) / 255;
	return RGB(r, g, b);
}

/* ============================================================================
 * NeverDark Seven: Authoritative Water Tuning Configuration
 * Zero magic constants. All bathymetric, wave dispersion, tidal ebb/flow,
 * caustic, and illumination parameters centralized and individually tunable.
 * ============================================================================ */

#define ND_WATER_MAX_OCTAVES 8

typedef struct {
	uint8_t shift;        /* Libnoise lattice shift (3..10 = 8px..1024px) */
	int16_t amp;          /* Wave height amplitude contribution (scaled / 16) */
	uint32_t seed;        /* Unique octave seed */
	uint8_t speed_num_x;  /* Dispersion phase drift speed in x: (anim * speed_num_x) >> speed_den */
	uint8_t speed_num_y;  /* Dispersion phase drift speed in y: (anim * speed_num_y) >> speed_den */
	uint8_t speed_den;    /* Dispersion divisor bit-shift */
	uint8_t morph_shift;  /* 3D noise morphing bit-shift (anim >> morph_shift) */
	int16_t offset_x;     /* Fixed world offset to prevent harmonic alignment */
	int16_t offset_y;
} nd_water_octave_t;

typedef struct {
	/* 1. Coastal Bathymetry & Continental Shelf */
	int shelf_width_units;      /* Width of coastal shelf in world units (384 units = 768px = 2.4 room tiles) */
	int shallow_depth_max;      /* Threshold for crystal shallows (d <= 5) */
	int shelf_depth_max;        /* Threshold for continental shelf (d <= 12) */
	int max_depth;              /* Maximum abyssal depth tier (28) */

	/* 2. Shoreline Ebb & Flow (Tidal Swash & Backwash Dynamics) */
	int foam_base_alpha;        /* Base opacity of shore foam during crest surge */
	int foam_surge_alpha;       /* Extra opacity contribution from wave elevation */
	int wet_sand_darken;        /* Darkening factor for wet sand during backwash (percent, e.g. 76 = 24% darker) */

	/* 3. Wave Spectrum & Physical Dispersion */
	int num_octaves;
	nd_water_octave_t octaves[ND_WATER_MAX_OCTAVES];

	/* 4. Subsurface Caustics */
	uint8_t caustic_shift;      /* Lattice resolution (3 = 8px) */
	uint8_t caustic_c1_speed;   /* Counter-drift speed 1 divisor (anim >> caustic_c1_speed) */
	uint8_t caustic_c2_speed;   /* Counter-drift speed 2 divisor (anim >> caustic_c2_speed) */
	int caustic_threshold;      /* Web ridge sharpness threshold */
	int caustic_max_alpha;      /* Maximum blending intensity */
	int caustic_min_d;          /* Minimum depth for caustics */
	int caustic_max_d;          /* Maximum depth for caustics */

	/* 5. Surface Hillshading & Sun Lighting */
	int diffuse_lit_mult;       /* Multiplier for sunward wave slopes */
	int diffuse_lit_max;        /* Clamped maximum highlight alpha */
	int shadow_mult;            /* Multiplier for wave shadows */
	int shadow_max;             /* Clamped maximum shadow alpha */
	int wave_diffuse_deadband;  /* Flat water deadband threshold */
} nd_water_config_t;

static const nd_water_config_t s_water_cfg = {
	/* 1. Coastal Bathymetry: gradual continental shelf spanning 320 units (2 full room tiles = 640px) */
	.shelf_width_units = 320,
	.shallow_depth_max = 5,
	.shelf_depth_max   = 12,
	.max_depth         = 28,

	/* 2. Shoreline Ebb & Flow: natural translucent swash & wet-sand backwash */
	.foam_base_alpha  = 55,
	.foam_surge_alpha = 45,
	.wet_sand_darken  = 82,

	/* 3. Wave Spectrum: 8-octave hierarchy matching terrain relief (1024px down to 8px)
	 * Dispersion scaling: phase velocity c proportional to sqrt(lambda).
	 * Macro swells roll rapidly across the sea; capillary ripples drift gently without boiling. */
	.num_octaves = 8,
	.octaves = {
		/* Shift 10: 1024px (~6 rooms) — continental ocean swell sweeps */
		{ .shift = 10, .amp = 48, .seed = 0x4F434530u, .speed_num_x = 4, .speed_num_y = 2, .speed_den = 1, .morph_shift = 6, .offset_x =   0, .offset_y =   0 },
		/* Shift 9:  512px (~3 rooms) — grand rolling sea swell */
		{ .shift =  9, .amp = 36, .seed = 0x4F434541u, .speed_num_x = 3, .speed_num_y = 3, .speed_den = 2, .morph_shift = 5, .offset_x =  17, .offset_y =   7 },
		/* Shift 8:  256px (~1.5 rooms) — major wave train traversing room boundaries */
		{ .shift =  8, .amp = 28, .seed = 0x5357454Cu, .speed_num_x = 2, .speed_num_y = 1, .speed_den = 1, .morph_shift = 5, .offset_x =  31, .offset_y =  13 },
		/* Shift 7:  128px (1 room) — mid-sea rolling swell */
		{ .shift =  7, .amp = 22, .seed = 0x57415645u, .speed_num_x = 3, .speed_num_y = 3, .speed_den = 3, .morph_shift = 4, .offset_x =  47, .offset_y =  23 },
		/* Shift 6:   64px (half room) — primary wind waves */
		{ .shift =  6, .amp = 16, .seed = 0x52495050u, .speed_num_x = 1, .speed_num_y = 1, .speed_den = 1, .morph_shift = 4, .offset_x =  61, .offset_y =  29 },
		/* Shift 5:   32px (2 cells) — surface chop */
		{ .shift =  5, .amp = 10, .seed = 0x46494E45u, .speed_num_x = 1, .speed_num_y = 1, .speed_den = 2, .morph_shift = 5, .offset_x =  79, .offset_y =  37 },
		/* Shift 4:   16px (1 cell) — gentle surface ripples */
		{ .shift =  4, .amp =  6, .seed = 0x57415452u, .speed_num_x = 1, .speed_num_y = 1, .speed_den = 3, .morph_shift = 5, .offset_x =  97, .offset_y =  43 },
		/* Shift 3:    8px (half cell) — capillary wavelets: slow gentle drift, zero jitter/boiling */
		{ .shift =  3, .amp =  4, .seed = 0x53505241u, .speed_num_x = 1, .speed_num_y = 1, .speed_den = 4, .morph_shift = 6, .offset_x = 113, .offset_y =  51 },
	},

	/* 4. Subsurface Caustics: delicate sunlight filigree */
	.caustic_shift     = 3,
	.caustic_c1_speed  = 2,
	.caustic_c2_speed  = 2,
	.caustic_threshold = 14,
	.caustic_max_alpha = 65,
	.caustic_min_d     = 2,
	.caustic_max_d     = 6,

	/* 5. Surface Hillshading */
	.diffuse_lit_mult      = 5,
	.diffuse_lit_max       = 85,
	.shadow_mult           = 5,
	.shadow_max            = 70,
	.wave_diffuse_deadband = 1,
};

static inline uint32_t lerp_rgb(uint32_t c1, uint32_t c2, int num, int den)
{
	if (num <= 0) return c1;
	if (num >= den) return c2;
	int r1 = (int)(c1 & 0xFF), g1 = (int)((c1 >> 8) & 0xFF), b1 = (int)((c1 >> 16) & 0xFF);
	int r2 = (int)(c2 & 0xFF), g2 = (int)((c2 >> 8) & 0xFF), b2 = (int)((c2 >> 16) & 0xFF);
	int r = r1 + ((r2 - r1) * num) / den;
	int g = g1 + ((g2 - g1) * num) / den;
	int b = b1 + ((b2 - b1) * num) / den;
	return RGB(r, g, b);
}

static inline uint32_t water_color_at_depth(int depth, int r, int gg, int b)
{
	/* Volumetric Beer-Lambert light extinction colors:
	 * Red and yellow wavelengths attenuate first, leaving brilliant tropical turquoise
	 * in the shallows, transitioning smoothly into coastal cerulean and deep sapphire. */
	static const uint32_t C_SHALLOW = RGB(64, 218, 235); /* Crystal shallows: brilliant Caribbean turquoise (d <= shallow_depth_max) */
	static const uint32_t C_SHELF   = RGB(52, 178, 228); /* Continental shelf: vibrant azure/cerulean (d = shallow..shelf) */
	static const uint32_t C_OUTER   = RGB(36, 122, 202); /* Coastal ocean: deep azure (d = shelf..22) */
	static const uint32_t C_ABYSS   = RGB(14, 50, 135);  /* Abyssal basin: deep royal sapphire (d >= 22) */

	uint32_t water_col;
	if (depth <= s_water_cfg.shallow_depth_max) {
		water_col = lerp_rgb(C_SHALLOW, C_SHELF, depth - 1, s_water_cfg.shallow_depth_max - 1);
	} else if (depth <= s_water_cfg.shelf_depth_max) {
		water_col = lerp_rgb(C_SHELF, C_OUTER, depth - s_water_cfg.shallow_depth_max,
		                     s_water_cfg.shelf_depth_max - s_water_cfg.shallow_depth_max);
	} else {
		int t = depth - s_water_cfg.shelf_depth_max;
		if (t > 10) t = 10;
		water_col = lerp_rgb(C_OUTER, C_ABYSS, t, 10);
	}
	return tint_col(water_col, r, gg, b);
}

static inline int water_alpha_at_depth(int depth)
{
	/* True exponential Beer-Lambert optical extinction:
	 * T(d) = exp(-sigma * d), alpha = 255 * (1 - T(d)).
	 * Shallows (d=1..3) are crystal clear (85%..64% transmission of the sandy seabed).
	 * Continental shelf gently darkens, reaching total optical extinction in the deep abyss. */
	static const uint8_t s_beer_lambert_alpha[29] = {
		0,
		38,   65,  92, 118, 142, 162, 178, 190, 200, 208,
		215, 221, 226, 230, 234, 237, 240, 242, 244, 246,
		248, 249, 250, 251, 252, 253, 254, 255
	};
	if (depth <= 0) return 0;
	if (depth >= s_water_cfg.max_depth) return 255;
	return (int)s_beer_lambert_alpha[depth];
}

typedef struct {
	int32_t xc, yc;          /* Center in world units */
	int32_t x0, y0, x1, y1;  /* Bounding box */
	uint8_t round_corners;   /* Bitmask: bit 0=NW, 1=NE, 2=SW, 3=SE round into water */
} nd_land_tile_t;

typedef struct {
	int32_t tile_xc, tile_yc;   /* Water tile center */
	int8_t sx, sy;              /* Direction towards the corner (+-1) */
} nd_land_jut_t;

static nd_land_tile_t s_active_land_tiles[64];
static int s_active_n_land_tiles = 0;

static nd_land_jut_t s_active_land_juts[64];
static int s_active_n_land_juts = 0;

static uint32_t s_last_coast_map_room = 0xFFFFFFFF;
static int16_t s_last_coast_map_pos[4] = { -32768, -32768, -32768, -32768 };

static void nd_coast_map_build(const nd_map_data_t *map)
{
	s_active_n_land_tiles = 0;
	s_active_n_land_juts = 0;
	if (!map || map->width == 0 || map->height == 0) return;

	int w = (int)map->width;
	int h = (int)map->height;
	int px = (int)map->player_x;
	int py = (int)map->player_y;

	for (int r = 0; r < h; r++) {
		for (int c = 0; c < w; c++) {
			int idx = r * w + c;
			uint8_t bio = map->tiles[idx].bio_idx;
			int is_w = ndw_is_water_ground(ndw_biome_style(bio).kind);

			int dcol = c - px;
			int drow = r - py;
			int32_t bx = (int32_t)(map->pos[0] + dcol) * NDW_TILE_W;
			int32_t by = -(int32_t)(map->pos[1] - drow) * NDW_TILE_H;
			int32_t xc = bx + NDW_TILE_W / 2;
			int32_t yc = by + NDW_TILE_H / 2;

			/* Check orthogonal neighbors: North (r-1), South (r+1), West (c-1), East (c+1) */
			int w_west = (c > 0) ? ndw_is_water_ground(ndw_biome_style(map->tiles[r * w + (c - 1)].bio_idx).kind) : is_w;
			int w_east = (c + 1 < w) ? ndw_is_water_ground(ndw_biome_style(map->tiles[r * w + (c + 1)].bio_idx).kind) : is_w;
			int w_north = (r > 0) ? ndw_is_water_ground(ndw_biome_style(map->tiles[(r - 1) * w + c].bio_idx).kind) : is_w;
			int w_south = (r + 1 < h) ? ndw_is_water_ground(ndw_biome_style(map->tiles[(r + 1) * w + c].bio_idx).kind) : is_w;

			if (!is_w) {
				/* Land tile: check which corners round into water */
				if (s_active_n_land_tiles < 64) {
					nd_land_tile_t *lt = &s_active_land_tiles[s_active_n_land_tiles++];
					lt->xc = xc; lt->yc = yc;
					lt->x0 = bx; lt->y0 = by;
					lt->x1 = bx + NDW_TILE_W; lt->y1 = by + NDW_TILE_H;
					uint8_t rc = 0;
					if (w_west && w_north) rc |= (1 << 0); /* NW */
					if (w_east && w_north) rc |= (1 << 1); /* NE */
					if (w_west && w_south) rc |= (1 << 2); /* SW */
					if (w_east && w_south) rc |= (1 << 3); /* SE */
					lt->round_corners = rc;
				}
			} else {
				/* Water tile: check if land juts into concave corners */
				if (!w_west && !w_north && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = -1; lj->sy = -1;
				}
				if (!w_east && !w_north && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = 1; lj->sy = -1;
				}
				if (!w_west && !w_south && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = -1; lj->sy = 1;
				}
				if (!w_east && !w_south && s_active_n_land_juts < 64) {
					nd_land_jut_t *lj = &s_active_land_juts[s_active_n_land_juts++];
					lj->tile_xc = xc; lj->tile_yc = yc;
					lj->sx = 1; lj->sy = 1;
				}
			}
		}
	}
}

static inline void nd_coast_map_build_if_needed(const nd_map_data_t *map)
{
	if (!map || map->width == 0 || map->height == 0) {
		s_active_n_land_tiles = 0;
		s_active_n_land_juts = 0;
		s_last_coast_map_room = 0xFFFFFFFF;
		return;
	}
	if (map->room_ref == s_last_coast_map_room &&
	    memcmp(map->pos, s_last_coast_map_pos, sizeof(s_last_coast_map_pos)) == 0) {
		return;
	}
	s_last_coast_map_room = map->room_ref;
	memcpy(s_last_coast_map_pos, map->pos, sizeof(s_last_coast_map_pos));
	nd_coast_map_build(map);
}

static inline int nd_global_land_dist(int32_t wx, int32_t wy)
{
	if (s_active_n_land_tiles == 0 && s_active_n_land_juts == 0) return 99999;
	int min_dist = 99999;

	/* 1. Distance to land tiles */
	for (int i = 0; i < s_active_n_land_tiles; i++) {
		const nd_land_tile_t *lt = &s_active_land_tiles[i];

		/* Quick bounding box cull with current min_dist */
		if (wx < lt->x0 - min_dist || wx > lt->x1 + min_dist ||
		    wy < lt->y0 - min_dist || wy > lt->y1 + min_dist)
			continue;

		int sx = (wx >= lt->xc) ? 1 : -1;
		int sy = (wy >= lt->yc) ? 1 : -1;
		int q_idx = (sy > 0 ? 2 : 0) | (sx > 0 ? 1 : 0);
		int rounds = (lt->round_corners >> q_idx) & 1;

		int px = (wx - lt->xc) * sx; /* >= 0 */
		int py = (wy - lt->yc) * sy; /* >= 0 */

		if (!rounds) {
			int dx = (px > 80) ? (px - 80) : 0;
			int dy = (py > 64) ? (py - 64) : 0;
			int d = ndw_isqrt(dx * dx + dy * dy);
			if (d < min_dist) {
				min_dist = d;
				if (min_dist == 0) return 0;
			}
		} else {
			/* Ellipse centered at (xc, yc) with rx=80, ry=64 */
			int py_scaled = (py * 5) / 4;
			int r = ndw_isqrt(px * px + py_scaled * py_scaled);
			if (r <= 80) {
				return 0; /* Inside land! */
			}
			int d = r - 80;
			if (d < min_dist) {
				min_dist = d;
			}
		}
	}

	/* 2. Distance to land juts (concave water corners where land juts in) */
	for (int i = 0; i < s_active_n_land_juts; i++) {
		const nd_land_jut_t *lj = &s_active_land_juts[i];
		int px = (wx - lj->tile_xc) * lj->sx;
		int py = (wy - lj->tile_yc) * lj->sy;
		if (px > 0 && py > 0) {
			int py_scaled = (py * 5) / 4;
			int r = ndw_isqrt(px * px + py_scaled * py_scaled);
			if (r >= 80) {
				return 0; /* Inside jutting land! */
			}
			int d = 80 - r;
			if (d < min_dist) {
				min_dist = d;
			}
		}
	}

	return min_dist;
}

/* Gradual continental shelf bathymetry: extends water shelf darkening
 * across a broad coastal shelf (s_water_cfg.shelf_width_units = 320 units)
 * to preserve expansive, luminous shallow Caribbean turquoise water near
 * the shore before gently transitioning into the deep ocean basin. */
static inline int nd_water_depth_at(int dist_units, int terrain_depth)
{
	if (dist_units <= 0) return 1;

	/* 1. Dean's Equilibrium Coastal Shelf Profile:
	 * h(y) = 1 + (D_max - 1) * (y / shelf_width)^(2/3) */
	int shelf_w = s_water_cfg.shelf_width_units;
	if (shelf_w <= 0) shelf_w = 384;
	int base_d;
	if (dist_units >= shelf_w) {
		base_d = s_water_cfg.max_depth;
	} else {
		int64_t d2 = (int64_t)dist_units * dist_units;
		int r = 0;
		while ((int64_t)(r + 1) * (r + 1) * (r + 1) <= d2) r++;
		base_d = 1 + ((s_water_cfg.max_depth - 1) * r) / 53;
	}

	/* 2. Continuous 3D bathymetric modulation from 8-octave terrain heightmap:
	 * Water can't discard terrain height. It must use it.
	 * terrain_depth = w_level - elev. Nominal ocean depth = 32.
	 * Shoreline stays pinned to shallow crystal clear water (d=1),
	 * with seabed topography ramping smoothly offshore across the shelf. */
	int delta = terrain_depth - 32;
	int blend_dist = dist_units < 192 ? dist_units : 192;
	int mod = (delta * blend_dist) / (192 * 6);

	int depth = base_d + mod;
	if (depth < 1) depth = 1;
	if (depth > s_water_cfg.max_depth) depth = s_water_cfg.max_depth;
	return depth;
}

static void render_water_cell(ctx_t *c, const ndw_ground_t *g, const int16_t pos[4],
                              int cx, int cy, uint32_t hash, uint32_t anim,
                              int r, int gg, int b)
{
	(void)anim;
	(void)hash;
	uint16_t world = (uint16_t)pos[3];
	int16_t w_level = nd_terrain_water_level(world);

	for (int uy = 0; uy < NDW_CELL_PX; uy++) {
		for (int ux = 0; ux < NDW_CELL_PX; ux++) {
			int px_in_tile = cx * NDW_CELL_PX + ux;
			int py_in_tile = cy * NDW_CELL_PX + uy;
			int32_t wx = (int32_t)pos[0] * NDW_TILE_W + px_in_tile;
			int32_t wy = -(int32_t)pos[1] * NDW_TILE_H + py_in_tile;
			int16_t elev = nd_terrain_height_hd(wx, wy, world);
			int terrain_depth = (int)(w_level - elev);
			if (terrain_depth < 1) terrain_depth = 1;

			int dist_units = s_water_cfg.shelf_width_units;
			if (s_active_n_land_tiles > 0 || s_active_n_land_juts > 0) {
				const ndw_warp_grid_t *wg = nd_seven_warp_grid(pos);
				int qx = px_in_tile * NDW_SUB;
				int qy = py_in_tile * NDW_SUB;
				int w_dx = 0, w_dy = 0;
				if (wg) ndw_warp_at(wg, qx, qy, &w_dx, &w_dy);
				int32_t wx_warped = wx + w_dx / NDW_FINE;
				int32_t wy_warped = wy + w_dy / NDW_FINE;
				dist_units = nd_global_land_dist(wx_warped, wy_warped);
			}

			int depth;
			if (g->kind == NDW_G_SHALLOW_WATER) {
				depth = 4;
			} else {
				depth = nd_water_depth_at(dist_units, terrain_depth);
			}
			if (depth < 1) depth = 1;

			/* Underlying sandy seabed with subtle micro-grain texture */
			int sx0 = c->ox + ux * SCALE;
			int sy0 = c->oy + uy * SCALE;
			uint32_t n = unit_noise(hash, ux, uy) & 31;
			uint32_t sand_base  = RGB(226, 200, 140);
			uint32_t sand_dark  = RGB(214, 188, 126);
			uint32_t sand_light = RGB(236, 212, 155);
			uint32_t seabed = (n < 2) ? sand_dark : (n == 2 ? sand_light : sand_base);
			seabed = tint_col(seabed, r, gg, b);

			uint32_t water_col = water_color_at_depth(depth, r, gg, b);
			int alpha = water_alpha_at_depth(depth);

			uint32_t final_pixel = alpha_blend_rgb(seabed, water_col, alpha);
			urect(c, ux, uy, 1, 1, final_pixel);

			if (s_active_chunk_depth) {
				uint8_t d_byte = (depth > 255) ? 255 : (uint8_t)depth;
				for (int dy = 0; dy < SCALE; dy++) {
					for (int dx = 0; dx < SCALE; dx++) {
						s_active_chunk_depth[(sy0 + dy) * TILE_PX_W + (sx0 + dx)] = d_byte;
					}
				}
			}
		}
	}
}

void ground_cell(uint32_t *fb, int w, int h, const ndw_ground_t *g,
                 const int16_t pos[4], int cx, int cy, int px, int py, uint32_t anim)
{
	ctx_t c = { fb, w, h, px, py };
	uint32_t hash = ndw_cell_hash(pos, cx, cy);
	uint32_t base, alt1, alt2, alt3 = 0;
	int r = g->r, gg = g->g, b = g->b;

	if (px >= w || py >= h || px + NDW_CELL_PX * SCALE <= 0 || py + NDW_CELL_PX * SCALE <= 0)
		return;

	if (g->kind == NDW_G_GRASS) {
		tint_t t = { g->r, g->g, g->b, 0 };
		blit_all(fb, w, h, NDS_GRASS, 0, 0, px, py, SCALE, &t);
		if ((hash % 100) < 6) {
			int id = NDS_DECOR_YEL + (int)((hash >> 8) & 1);
			const nd_seven_sprite_rect_t *sr = &nd_seven_sprite_rects[id];
			blit_all(fb, w, h, id, sr->w / 2, sr->h - 1,
			         px + (4 + (int)((hash >> 12) & 7)) * SCALE, py + 14 * SCALE, SCALE, NULL);
		}
		return;
	}

	if (ndw_is_water_ground(g->kind)) {
		render_water_cell(&c, g, pos, cx, cy, hash, anim, r, gg, b);
		return;
	}

	switch (g->kind) {
	case NDW_G_SAND:
		base = RGB(226, 200, 140); alt1 = RGB(210, 182, 122); alt2 = RGB(238, 214, 160);
		break;
	case NDW_G_SNOW:
		base = RGB(234, 240, 250); alt1 = RGB(210, 222, 240); alt2 = RGB(252, 252, 255);
		break;
	case NDW_G_TUNDRA:
		base = RGB(150, 166, 148); alt1 = RGB(128, 148, 126); alt2 = RGB(180, 190, 176);
		break;
	default: /* ash */
		base = RGB(54, 48, 52); alt1 = RGB(40, 36, 40); alt2 = RGB(76, 66, 68);
		break;
	}

	base = tint_col(base, r, gg, b);
	alt1 = tint_col(alt1, r, gg, b);
	alt2 = tint_col(alt2, r, gg, b);
	if (alt3) alt3 = tint_col(alt3, r, gg, b);

	urect(&c, 0, 0, NDW_CELL_PX, NDW_CELL_PX, base);
	for (int uy = 0; uy < NDW_CELL_PX; uy++)
		for (int ux = 0; ux < NDW_CELL_PX; ux++) {
			uint32_t n = unit_noise(hash, ux, uy) & 31;
			if (n < 2) {
				urect(&c, ux, uy, 1, 1, alt1);
			} else if (n == 2) {
				urect(&c, ux, uy, 1, 1, alt2);
			}
		}

	if (g->kind == NDW_G_SAND && (((hash >> 8) & 3) == 0)) {
		static const int8_t arc[8] = { 1, 0, 0, -1, -1, 0, 0, 1 };
		int ux0 = 2 + (int)((hash >> 12) & 3), uy0 = 5 + (int)((hash >> 15) & 7);
		for (int k = 0; k < 8; k++) {
			urect(&c, ux0 + k, uy0 + arc[k], 1, 1, shade(alt1, 92));
			urect(&c, ux0 + k, uy0 + arc[k] - 1, 1, 1, alt2);
		}
	}
	if (g->kind == NDW_G_ASH && (hash % 23) == 0) {
		int ux0 = 3 + (int)((hash >> 8) & 3), uy0 = 4 + (int)((hash >> 12) & 7);
		for (int k = 0; k < 8; k++)
			urect(&c, ux0 + k, uy0 + ((k >> 1) & 1), 1, 1, RGB(226, 96, 28));
	}
	if (g->kind == NDW_G_SNOW && (hash % 17) == 0)
		urect(&c, 3 + (int)((hash >> 8) & 7), 3 + (int)((hash >> 12) & 7), 2, 1, RGB(255, 255, 255));
}

#define BORDER_CACHE 64
typedef struct {
	int16_t pos[4];
	uint8_t used;
	uint8_t act_valid;
	uint64_t act_mask;
	uint8_t act[NDW_ROWS * NDW_COLS];
	ndw_warp_grid_t grid;
} border_ent_t;
static border_ent_t s_border[BORDER_CACHE];
static unsigned s_border_next;

static border_ent_t *border_find(const int16_t pos[4])
{
	border_ent_t *e;

	for (int i = 0; i < BORDER_CACHE; i++)
		if (s_border[i].used && !memcmp(s_border[i].pos, pos, sizeof(s_border[i].pos)))
			return &s_border[i];
	e = &s_border[s_border_next++ % BORDER_CACHE];
	memcpy(e->pos, pos, sizeof(e->pos));
	e->used = 1;
	e->act_valid = 0;
	ndw_warp_grid_fill(&e->grid, pos);
	return e;
}

const ndw_warp_grid_t *nd_seven_warp_grid(const int16_t pos[4])
{
	return &border_find(pos)->grid;
}

static int border_q(int cell, int s)
{
	return cell * NDW_CELL_PX * NDW_SUB + (s * 2 + 1) * NDW_SUB / (2 * SCALE);
}

static const uint8_t *border_active(border_ent_t *e, const uint8_t grp[9], unsigned no_blend)
{
	uint64_t key = (uint64_t)no_blend << 40;

	for (int k = 0; k < 9; k++)
		key |= (uint64_t)(grp[k] & 15) << (4 * k);
	if (e->act_valid && e->act_mask == key)
		return e->act;
	for (int cy = 0; cy < NDW_ROWS; cy++)
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int any = 0;

			for (int sy = 0; sy < NDW_CELL_PX * SCALE && !any; sy++)
				for (int sx = 0; sx < NDW_CELL_PX * SCALE && !any; sx++) {
					uint16_t w[9];
					any = ndw_ground_weights(&e->grid, border_q(cx, sx), border_q(cy, sy), grp, no_blend, w);
				}
			e->act[cy * NDW_COLS + cx] = (uint8_t)any;
		}
	e->act_mask = key;
	e->act_valid = 1;
	return e->act;
}

static int same_ground(const ndw_ground_t *a, const ndw_ground_t *b)
{
	return a->kind == b->kind && a->r == b->r && a->g == b->g && a->b == b->b;
}

void ground_blend_nbiomes(uint32_t *fb, int w, int h, const uint8_t nbiomes[9],
                          const int16_t pos[4], const ndw_tile_t *own,
                          int tx, int ty, uint8_t *depth_grid)
{
	static uint32_t scratch[9][NDW_CELL_PX * SCALE * NDW_CELL_PX * SCALE];
	const int cpx = NDW_CELL_PX * SCALE;
	ndw_ground_t style[9];
	unsigned no_blend = 0;
	uint8_t grp[9];
	const uint8_t *act;
	border_ent_t *ent;
	int differs = 0;

	style[4] = ndw_biome_style(own->biome);
	for (int k = 0; k < 9; k++) {
		style[k] = ndw_biome_style(nbiomes ? nbiomes[k] : own->biome);
		differs |= !same_ground(&style[k], &style[4]);
		int di = (k % 3) - 1, dj = (k / 3) - 1;
		if (ndw_is_water_ground(style[k].kind))
			no_blend |= NDW_WATER_BIT(di, dj);
	}
	for (int k = 0; k < 9; k++) {
		grp[k] = (uint8_t)k;
		for (int j = 0; j < k; j++)
			if (same_ground(&style[j], &style[k])) {
				grp[k] = grp[j];
				break;
			}
	}
	if (!differs)
		return;
	ent = border_find(pos);
	act = border_active(ent, grp, no_blend);

	for (int cy = 0; cy < NDW_ROWS; cy++)
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int px = tx + cx * cpx, py = ty + cy * cpx;
			int have[9] = { 0 };

			if (!act[cy * NDW_COLS + cx])
				continue;
			if (px >= w || py >= h || px + cpx <= 0 || py + cpx <= 0)
				continue;
			for (int sy = 0; sy < cpx; sy++) {
				int y = py + sy;
				if (y < 0 || y >= h)
					continue;
				for (int sx = 0; sx < cpx; sx++) {
					int x = px + sx;
					uint16_t wt[9];
					int own_k = 4;

					if (x < 0 || x >= w)
						continue;
					if (!ndw_ground_weights(&ent->grid, border_q(cx, sx), border_q(cy, sy), grp, no_blend, wt))
						continue;
					for (int k = 0; k < 9; k++)
						if (wt[k])
							own_k = k;
					if (own_k == 4)
						continue;

					if (!have[own_k]) {
						ground_cell(scratch[own_k], cpx, cpx, &style[own_k], pos, cx, cy, 0, 0, nd_seven_get_anim());
						have[own_k] = 1;
					}
					fb[y * w + x] = scratch[own_k][sy * cpx + sx];
					if (depth_grid) {
						if (!ndw_is_water_ground(style[own_k].kind)) {
							depth_grid[y * w + x] = 0; /* Dry land encroaching */
						} else {
							int32_t wx = (int32_t)pos[0] * NDW_TILE_W + (cx * NDW_CELL_PX + sx / SCALE);
							int32_t wy = -(int32_t)pos[1] * NDW_TILE_H + (cy * NDW_CELL_PX + sy / SCALE);
							uint16_t world = (uint16_t)pos[3];
							int16_t elev = nd_terrain_height_hd(wx, wy, world);
							int16_t w_level = nd_terrain_water_level(world);
							int terrain_depth = (int)(w_level - elev);
							if (terrain_depth < 1) terrain_depth = 1;

							int qx = border_q(cx, sx);
							int qy = border_q(cy, sy);
							int w_dx = 0, w_dy = 0;
							ndw_warp_at(&ent->grid, qx, qy, &w_dx, &w_dy);
							int32_t wx_warped = wx + w_dx / NDW_FINE;
							int32_t wy_warped = wy + w_dy / NDW_FINE;

							int dist_units = nd_global_land_dist(wx_warped, wy_warped);
							depth_grid[y * w + x] = (uint8_t)nd_water_depth_at(dist_units, terrain_depth);
						}
					}
				}
			}
		}
}

void ground_blend(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                  int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                  int tx, int ty)
{
	uint8_t nbiomes[9];
	nbiomes[4] = own->biome;
	for (int dj = -1; dj <= 1; dj++)
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (!di && !dj) continue;
			ndw_tile_t nt;
			int16_t np[4];
			if (st) {
				nd_seven_tile_at(st, ti + di, tj + dj, &nt, np);
				nbiomes[k] = nt.biome;
			} else {
				nbiomes[k] = own->biome;
			}
		}
	ground_blend_nbiomes(fb, w, h, nbiomes, pos, own, tx, ty, NULL);
}

#define TILE_PX_W (NDW_TILE_W * SCALE)
#define TILE_PX_H (NDW_TILE_H * SCALE)
#define CHUNK_HASH_BITS 6
#define CHUNK_HASH_BUCKETS (1 << CHUNK_HASH_BITS)

static nd_room_chunk_t *s_chunk_buckets[CHUNK_HASH_BUCKETS];
static uint32_t s_current_frame = 1;
static int s_chunk_count = 0;
static int s_chunk_alloc_count = 0;
static int s_chunk_free_count = 0;

static inline unsigned chunk_hash(const int16_t pos[4])
{
	uint32_t h = (uint32_t)pos[0] * 73856093u ^
	             (uint32_t)pos[1] * 19349663u ^
	             (uint32_t)pos[2] * 83492791u ^
	             (uint32_t)pos[3] * 39916801u;
	return (unsigned)(h & (CHUNK_HASH_BUCKETS - 1));
}

static nd_room_chunk_t *chunk_find(const int16_t pos[4], const uint8_t nbiomes[9])
{
	unsigned bucket = chunk_hash(pos);
	for (nd_room_chunk_t *c = s_chunk_buckets[bucket]; c; c = c->next) {
		if (memcmp(c->pos, pos, sizeof(c->pos)) == 0) {
			if (nbiomes && memcmp(c->nbiomes, nbiomes, sizeof(c->nbiomes)) != 0) {
				return NULL; /* biomes changed, needs regeneration */
			}
			return c;
		}
	}
	return NULL;
}

const nd_room_chunk_t *nd_seven_chunk_find(const int16_t pos[4])
{
	return chunk_find(pos, NULL);
}

int nd_seven_pos_in_map(const int16_t pos[4], const nd_map_data_t *map)
{
	if (!map || map->width == 0 || map->height == 0)
		return 0;
	if (pos[2] != map->pos[2] || pos[3] != map->pos[3])
		return 0;

	int ti = (int)pos[0] - (int)map->pos[0];
	int tj = (int)map->pos[1] - (int)pos[1];
	int col = (int)map->player_x + ti;
	int row = (int)map->player_y + tj;

	return (col >= 0 && col < (int)map->width && row >= 0 && row < (int)map->height);
}

void nd_seven_ground_frame_begin(void)
{
	s_current_frame++;
	if (s_current_frame == 0) s_current_frame = 1;
}

void nd_seven_ground_frame_end(const nd_map_data_t *map)
{
	/* Active view window chunk retention & eager eviction:
	 * If map is provided, retain any chunk within the active 7x7 view window (st->map_data).
	 * Eagerly free any chunk whose coordinates have exited the 7x7 view window.
	 * If map is NULL, fallback to checking c->last_frame >= s_current_frame. */
	for (int b = 0; b < CHUNK_HASH_BUCKETS; b++) {
		nd_room_chunk_t **pp = &s_chunk_buckets[b];
		while (*pp) {
			nd_room_chunk_t *c = *pp;
			int keep = 0;
			if (map && map->width > 0 && map->height > 0) {
				keep = nd_seven_pos_in_map(c->pos, map);
			} else {
				keep = (c->last_frame >= s_current_frame);
			}

			if (!keep) {
				*pp = c->next;
				if (c->pixels) free(c->pixels);
				if (c->depth) free(c->depth);
				free(c);
				s_chunk_count--;
				s_chunk_free_count++;
			} else {
				pp = &c->next;
			}
		}
	}
}

void nd_seven_ground_cache_forget(void)
{
	s_active_n_land_tiles = 0;
	s_active_n_land_juts = 0;
	s_last_coast_map_room = 0xFFFFFFFF;
	for (int b = 0; b < CHUNK_HASH_BUCKETS; b++) {
		nd_room_chunk_t *c = s_chunk_buckets[b];
		while (c) {
			nd_room_chunk_t *next = c->next;
			if (c->pixels) free(c->pixels);
			if (c->depth) free(c->depth);
			free(c);
			c = next;
		}
		s_chunk_buckets[b] = NULL;
	}
	s_chunk_count = 0;
	s_chunk_alloc_count = 0;
	s_chunk_free_count = 0;
}

int nd_seven_chunk_count(void)
{
	return s_chunk_count;
}

int nd_seven_chunk_alloc_count(void)
{
	return s_chunk_alloc_count;
}

int nd_seven_chunk_free_count(void)
{
	return s_chunk_free_count;
}

static nd_room_chunk_t *chunk_get_or_create(const int16_t pos[4],
                                            const ndw_tile_t *own,
                                            const uint8_t nbiomes[9],
                                            const nd_map_data_t *map,
                                            int ti, int tj)
{
	(void)ti;
	(void)tj;
	atlas_load();
	nd_room_chunk_t *chunk = chunk_find(pos, nbiomes);
	if (chunk) {
		chunk->last_frame = s_current_frame;
		return chunk;
	}

	/* Evict any old chunk at this pos (e.g. stale biomes) */
	unsigned bucket = chunk_hash(pos);
	nd_room_chunk_t **pp = &s_chunk_buckets[bucket];
	while (*pp) {
		nd_room_chunk_t *c = *pp;
		if (memcmp(c->pos, pos, sizeof(c->pos)) == 0) {
			*pp = c->next;
			if (c->pixels) free(c->pixels);
			if (c->depth) free(c->depth);
			free(c);
			s_chunk_count--;
			s_chunk_free_count++;
			break;
		}
		pp = &c->next;
	}

	/* Allocate new room chunk */
	chunk = (nd_room_chunk_t *)calloc(1, sizeof(nd_room_chunk_t));
	if (!chunk) return NULL;
	chunk->pixels = (uint32_t *)malloc(TILE_PX_W * TILE_PX_H * sizeof(uint32_t));
	if (!chunk->pixels) {
		free(chunk);
		return NULL;
	}
	memcpy(chunk->pos, pos, sizeof(chunk->pos));
	if (nbiomes) {
		memcpy(chunk->nbiomes, nbiomes, sizeof(chunk->nbiomes));
	} else if (own) {
		memset(chunk->nbiomes, own->biome, sizeof(chunk->nbiomes));
	}
	chunk->last_frame = s_current_frame;

	uint8_t own_biome = own ? own->biome : (nbiomes ? nbiomes[4] : 0);
	int has_water = ndw_is_water_ground(ndw_biome_style(own_biome).kind);
	if (nbiomes) {
		for (int k = 0; k < 9; k++) {
			if (ndw_is_water_ground(ndw_biome_style(nbiomes[k]).kind))
				has_water = 1;
		}
	}
	chunk->has_water = (uint8_t)has_water;
	if (has_water) {
		chunk->depth = (uint8_t *)calloc(TILE_PX_W * TILE_PX_H, sizeof(uint8_t));
	}
	s_active_chunk_depth = chunk->depth;

	nd_coast_map_build_if_needed(map);
	uint8_t land_mask = 0;
	uint8_t cgrp[9];
	for (int k = 0; k < 9; k++) {
		uint8_t b = nbiomes ? nbiomes[k] : own_biome;
		ndw_ground_t st_b = ndw_biome_style(b);
		int is_water = ndw_is_water_ground(st_b.kind);
		cgrp[k] = is_water ? 0 : 1;
		if (!is_water)
			land_mask |= (1 << k);
	}
	s_active_chunk_land_mask = land_mask;
	memcpy(s_active_chunk_grp, cgrp, sizeof(s_active_chunk_grp));

	ndw_ground_t g = ndw_biome_style(own_biome);
	int is_water_tile = ndw_is_water_ground(g.kind);

	for (int cy = 0; cy < NDW_ROWS; cy++) {
		for (int cx = 0; cx < NDW_COLS; cx++) {
			int cell_px = cx * NDW_CELL_PX * SCALE;
			int cell_py = cy * NDW_CELL_PX * SCALE;

			if (is_water_tile) {
				/* 1. Render authentic ground substrate first */
				ndw_ground_t substrate_style = { NDW_G_SAND, 128, 128, 128 };
				if (land_mask && nbiomes) {
					/* Find nearest land neighbor for authentic local substrate */
					int px_w = (cx * NDW_CELL_PX + NDW_CELL_PX / 2) * NDW_FINE;
					int py_w = (cy * NDW_CELL_PX + NDW_CELL_PX / 2) * NDW_FINE;
					int max_d = -999999;
					int best_k = 4;
					for (int dj = -1; dj <= 1; dj++) {
						for (int di = -1; di <= 1; di++) {
							int k = (dj + 1) * 3 + (di + 1);
							if (land_mask & (1 << k)) {
								int d = ndw_tile_depth(cgrp, 0, di, dj, px_w, py_w);
								if (d > max_d) { max_d = d; best_k = k; }
							}
						}
					}
					int dist_u = (-max_d) / NDW_FINE;
					if (dist_u < 24)
						substrate_style = ndw_biome_style(nbiomes[best_k]);
				}
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &substrate_style, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());

				/* 2. Composite volumetric water layer over authentic substrate */
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &g, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());
			} else {
				ground_cell(chunk->pixels, TILE_PX_W, TILE_PX_H, &g, pos, cx, cy,
				            cell_px, cell_py, nd_seven_get_anim());
			}
		}
	}
	s_active_chunk_depth = NULL;
	if (own) {
		ground_blend_nbiomes(chunk->pixels, TILE_PX_W, TILE_PX_H, nbiomes, pos, own, 0, 0, chunk->depth);
	}
	s_active_chunk_land_mask = 0;

	chunk->next = s_chunk_buckets[bucket];
	s_chunk_buckets[bucket] = chunk;
	s_chunk_count++;
	s_chunk_alloc_count++;
	return chunk;
}

const nd_room_chunk_t *nd_seven_chunk_ensure(const nd_map_data_t *map, int col, int row)
{
	if (!map || col < 0 || col >= (int)map->width || row < 0 || row >= (int)map->height)
		return NULL;

	nd_coast_map_build_if_needed(map);

	int16_t pos[4];
	pos[0] = (int16_t)(map->pos[0] + (col - (int)map->player_x));
	pos[1] = (int16_t)(map->pos[1] - (row - (int)map->player_y));
	pos[2] = map->pos[2];
	pos[3] = map->pos[3];

	const nd_room_chunk_t *existing = nd_seven_chunk_find(pos);
	if (existing) {
		((nd_room_chunk_t *)existing)->last_frame = s_current_frame;
		return existing;
	}

	int idx = row * (int)map->width + col;
	ndw_tile_t own = nd_seven_convert_tile(&map->tiles[idx]);

	uint8_t nbiomes[9];
	nbiomes[4] = own.biome;
	for (int dj = -1; dj <= 1; dj++) {
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (di == 0 && dj == 0) continue;
			int nc = col + di;
			int nr = row + dj;
			if (nc >= 0 && nc < (int)map->width && nr >= 0 && nr < (int)map->height) {
				nbiomes[k] = map->tiles[nr * (int)map->width + nc].bio_idx;
			} else {
				nbiomes[k] = own.biome;
			}
		}
	}

	int ti = col - (int)map->player_x;
	int tj = row - (int)map->player_y;
	return chunk_get_or_create(pos, &own, nbiomes, map, ti, tj);
}

void nd_seven_ground_tile_render(uint32_t *fb, int w, int h, const nd_client_state_t *st,
                                 int ti, int tj, const int16_t pos[4], const ndw_tile_t *own,
                                 int tx, int ty)
{
	if (tx + TILE_PX_W <= 0 || tx >= w || ty + TILE_PX_H <= 0 || ty >= h)
		return;

	if (st) {
		nd_coast_map_build_if_needed(&st->map_data);
	}

	uint8_t nbiomes[9];
	nbiomes[4] = own->biome;

	for (int dj = -1; dj <= 1; dj++) {
		for (int di = -1; di <= 1; di++) {
			int k = (dj + 1) * 3 + (di + 1);
			if (di == 0 && dj == 0) continue;
			ndw_tile_t nt;
			int16_t np[4];
			nd_seven_tile_at(st, ti + di, tj + dj, &nt, np);
			nbiomes[k] = nt.biome;
		}
	}

	nd_room_chunk_t *chunk = chunk_get_or_create(pos, own, nbiomes, st ? &st->map_data : NULL, ti, tj);
	if (!chunk || !chunk->pixels)
		return;

	int src_x0 = 0, src_y0 = 0;
	int dst_x0 = tx, dst_y0 = ty;
	int bw = TILE_PX_W;
	int bh = TILE_PX_H;

	if (dst_x0 < 0) {
		src_x0 -= dst_x0;
		bw += dst_x0;
		dst_x0 = 0;
	}
	if (dst_y0 < 0) {
		src_y0 -= dst_y0;
		bh += dst_y0;
		dst_y0 = 0;
	}
	if (dst_x0 + bw > w)
		bw = w - dst_x0;
	if (dst_y0 + bh > h)
		bh = h - dst_y0;

	if (bw <= 0 || bh <= 0)
		return;

	if (chunk->depth) {
		uint32_t anim = nd_seven_get_anim();
		/* Seamless world-space anchor: room pos in tile pixel coordinates (TILE_PX_W x TILE_PX_H = 320 x 256) */
		int base_px_x = (int)pos[0] * TILE_PX_W;
		int base_px_y = -(int)pos[1] * TILE_PX_H;

		/* Caustic counter-drifts */
		int16_t c1_dx = (int16_t)(anim >> s_water_cfg.caustic_c1_speed);
		int16_t c1_dy = (int16_t)(anim >> s_water_cfg.caustic_c1_speed);
		int16_t c2_dx = (int16_t)(-anim >> s_water_cfg.caustic_c2_speed);
		int16_t c2_dy = (int16_t)(anim >> (s_water_cfg.caustic_c2_speed - 1));

		/* Surf foam drift: travels shoreward */
		int16_t surf_dx = (int16_t)((anim * 3) >> 2);
		int16_t surf_dy = (int16_t)((anim * 3) >> 3);
		int16_t surf_z  = (int16_t)(anim >> 4);

		noise_wcache_t c_oct[ND_WATER_MAX_OCTAVES], c_c1, c_c2, c_surf;
		for (int i = 0; i < s_water_cfg.num_octaves; i++) noise_wcache_init(&c_oct[i]);
		noise_wcache_init(&c_c1);
		noise_wcache_init(&c_c2);
		noise_wcache_init(&c_surf);

		static uint32_t w_oct[ND_WATER_MAX_OCTAVES][TILE_PX_W + 4];
		static uint32_t c1_row[TILE_PX_W], c2_row[TILE_PX_W], surf_row[TILE_PX_W];
		static int h_prev[TILE_PX_W + 4], h_curr[TILE_PX_W + 4];

		/* Precompute octave displacement and time offsets */
		int16_t oct_z[ND_WATER_MAX_OCTAVES];
		int16_t oct_dx[ND_WATER_MAX_OCTAVES];
		int16_t oct_dy[ND_WATER_MAX_OCTAVES];
		for (int i = 0; i < s_water_cfg.num_octaves; i++) {
			const nd_water_octave_t *oct = &s_water_cfg.octaves[i];
			oct_z[i] = (int16_t)(anim >> oct->morph_shift);
			oct_dx[i] = (int16_t)((anim * oct->speed_num_x) >> oct->speed_den);
			oct_dy[i] = (int16_t)((anim * oct->speed_num_y) >> oct->speed_den);
		}

		/* Evaluate row r = -1 to initialize h_prev so row r = 0 has seamless continuous slope */
		int gy_prev = base_px_y + src_y0 - 1;
		int gx_start = base_px_x + src_x0 - 1;
		for (int i = 0; i < s_water_cfg.num_octaves; i++) {
			const nd_water_octave_t *oct = &s_water_cfg.octaves[i];
			int16_t p[3] = {
				(int16_t)(oct_z[i] + oct->offset_y),
				(int16_t)(gy_prev - oct_dy[i] + oct->offset_y),
				(int16_t)(gx_start - oct_dx[i] + oct->offset_x)
			};
			noise_wrow(w_oct[i], bw + 2, p, 1, oct->shift, 8, oct->seed, 3, NOISE_FADE, &c_oct[i]);
		}
		for (int c = 0; c < bw + 2; c++) {
			int total_h = 0;
			for (int i = 0; i < s_water_cfg.num_octaves; i++) {
				int v = (int)(w_oct[i][c] >> 16) - 128;
				total_h += (v * s_water_cfg.octaves[i].amp) / 16;
			}
			h_prev[c] = total_h;
		}

		for (int r = 0; r < bh; r++) {
			int y_in_tile = (src_y0 + r);
			const uint32_t *src_row = &chunk->pixels[y_in_tile * TILE_PX_W + src_x0];
			const uint8_t *depth_row = &chunk->depth[y_in_tile * TILE_PX_W + src_x0];
			uint32_t *dst_row = &fb[(dst_y0 + r) * w + dst_x0];

			/* Scan row depth occupancy to skip idle noise layers */
			int has_caustic = 0, has_surf = 0, has_swell = 0;
			for (int c = 0; c < bw; c++) {
				uint8_t d = depth_row[c];
				if (d >= s_water_cfg.caustic_min_d && d <= s_water_cfg.caustic_max_d) has_caustic = 1;
				if (d >= 1 && d <= 2) has_surf = 1;
				if (d >= 1) has_swell = 1;
			}

			if (!has_caustic && !has_surf && !has_swell) {
				memcpy(dst_row, src_row, (size_t)bw * sizeof(uint32_t));
				continue;
			}

			int gy = base_px_y + y_in_tile;
			int gx = base_px_x + src_x0;

			/* 1. Waves: multi-scale elevation cascade from 1024px macro swell to 8px wavelets */
			if (has_swell) {
				for (int i = 0; i < s_water_cfg.num_octaves; i++) {
					const nd_water_octave_t *oct = &s_water_cfg.octaves[i];
					int16_t p[3] = {
						(int16_t)(oct_z[i] + oct->offset_y),
						(int16_t)(gy - oct_dy[i] + oct->offset_y),
						(int16_t)(gx_start - oct_dx[i] + oct->offset_x)
					};
					noise_wrow(w_oct[i], bw + 2, p, 1, oct->shift, 8, oct->seed, 3, NOISE_FADE, &c_oct[i]);
				}

				for (int c = 0; c < bw + 2; c++) {
					int total_h = 0;
					for (int i = 0; i < s_water_cfg.num_octaves; i++) {
						int v = (int)(w_oct[i][c] >> 16) - 128;
						total_h += (v * s_water_cfg.octaves[i].amp) / 16;
					}
					h_curr[c] = total_h;
				}
			}

			/* 2. Caustics: fine subtle network (8px detail) */
			if (has_caustic) {
				int16_t z_c = (int16_t)(anim >> 2);
				int16_t p_c1[3] = { z_c, (int16_t)(gy + c1_dy), (int16_t)(gx + c1_dx) };
				noise_wrow(c1_row, bw, p_c1, 1, s_water_cfg.caustic_shift, 8, 0x43415553u, 3, NOISE_FADE, &c_c1);

				int16_t p_c2[3] = { z_c, (int16_t)(gy + c2_dy - 9), (int16_t)(gx + c2_dx + 7) };
				noise_wrow(c2_row, bw, p_c2, 1, s_water_cfg.caustic_shift, 8, 0x54494353u, 3, NOISE_FADE, &c_c2);
			}

			/* 3. Surf foam: dynamic travelling foam along shore */
			if (has_surf) {
				int16_t p_surf[3] = { surf_z, (int16_t)(gy - surf_dy), (int16_t)(gx - surf_dx) };
				noise_wrow(surf_row, bw, p_surf, 1, 4, 8, 0x53555246u, 3, NOISE_FADE, &c_surf);
			}

			for (int c = 0; c < bw; c++) {
				uint8_t d = depth_row[c];
				if (d == 0) {
					dst_row[c] = src_row[c];
					continue;
				}

				uint32_t col = src_row[c];

				/* Caustics (d = 2..6): very delicate, faint sunlight shimmer on seabed */
				if (d >= s_water_cfg.caustic_min_d && d <= s_water_cfg.caustic_max_d) {
					int diff = abs((int)(c1_row[c] >> 16) - (int)(c2_row[c] >> 16));
					if (diff < s_water_cfg.caustic_threshold) {
						int k = s_water_cfg.caustic_threshold - diff;
						int c_int = (k * k * (s_water_cfg.caustic_max_d + 1 - d) * 3) >> 4;
						if (c_int > s_water_cfg.caustic_max_alpha) c_int = s_water_cfg.caustic_max_alpha;
						col = alpha_blend_rgb(col, RGB(240, 252, 255), c_int);
					}
				}

				/* Shoreline Ebb & Flow: natural translucent swash & wet-sand backwash */
				if (d <= 2) {
					int surf = (int)(surf_row[c] >> 16);
					int h = h_curr[c + 1];
					if (d == 1) {
						if (h > 0 && surf > 140) {
							/* Flow / Swash: wave crest surges up with delicate, translucent white froth */
							int surge = (h * s_water_cfg.foam_surge_alpha) / 16;
							int foam_alpha = s_water_cfg.foam_base_alpha + surge + (surf - 140) / 2;
							if (foam_alpha > 120) foam_alpha = 120;
							col = alpha_blend_rgb(col, RGB(255, 255, 255), foam_alpha);
						} else if (h < -6) {
							/* Strong backwash recession: wet sand glisten */
							col = shade(col, s_water_cfg.wet_sand_darken);
							if (surf > 195) {
								int trail_alpha = (surf - 195);
								col = alpha_blend_rgb(col, RGB(245, 250, 255), trail_alpha);
							}
						}
					} else if (d == 2) {
						if (h > 1 && surf > 150) {
							/* Leading edge of incoming wave crest */
							int c_alpha = (surf - 150) / 2 + h;
							if (c_alpha > 75) c_alpha = 75;
							col = alpha_blend_rgb(col, RGB(225, 250, 255), c_alpha);
						}
					}
				}

				/* Wave slope lighting (d >= 2): continuous 3D hillshading across all pixels including borders */
				if (d >= 2) {
					int dh_dx = h_curr[c + 2] - h_curr[c];
					int dh_dy = h_curr[c + 1] - h_prev[c + 1];
					int diffuse = (-dh_dx - dh_dy);

					/* Natural sun hillshading on multi-scale water surface */
					if (diffuse > s_water_cfg.wave_diffuse_deadband) {
						int lit = diffuse * s_water_cfg.diffuse_lit_mult;
						if (lit > s_water_cfg.diffuse_lit_max) lit = s_water_cfg.diffuse_lit_max;
						uint32_t crest_col = (d <= s_water_cfg.shallow_depth_max) ? RGB(140, 220, 250) : RGB(60, 150, 225);
						col = alpha_blend_rgb(col, crest_col, lit);
					} else if (diffuse < -s_water_cfg.wave_diffuse_deadband) {
						int shd = (-diffuse) * s_water_cfg.shadow_mult;
						if (shd > s_water_cfg.shadow_max) shd = s_water_cfg.shadow_max;
						col = alpha_blend_rgb(col, RGB(6, 22, 65), shd);
					}
				}

				dst_row[c] = col;
			}
			memcpy(h_prev, h_curr, (size_t)(bw + 2) * sizeof(int));
		}
	} else {
		for (int r = 0; r < bh; r++) {
			const uint32_t *src_row = &chunk->pixels[(src_y0 + r) * TILE_PX_W + src_x0];
			uint32_t *dst_row = &fb[(dst_y0 + r) * w + dst_x0];
			memcpy(dst_row, src_row, (size_t)bw * sizeof(uint32_t));
		}
	}
}
