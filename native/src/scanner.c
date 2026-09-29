#include <jni.h>
JNIEXPORT jint JNICALL Java_com_himig_offline_RgScan_nativeScanFd(JNIEnv* env, jobject thiz, jint fd, jstring outJson);
JNIEXPORT jint JNICALL Java_com_himig_offline_RgScan_nativeWriteTags(JNIEnv* env, jobject thiz, jint fd, jdouble trackGain, jdouble trackPeak, jdouble albumGain, jdouble albumPeak);
