#include <gdnative_api_struct.gen.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/select.h>
#endif
#define MODELDIR "addons/speechtotext/model"
#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <pocketsphinx.h>

const int buffer_size = 2048;
int16 adbuf[2048];
int32 ad_n_samples;
char *result;
enum status_type {
    starting,
    ready,
    enabled,
    in_recognition,
    recognition_finished,
    self_destructing
} status;
pthread_t recognition_thread;
static ad_rec_t *ad;
static ps_decoder_t *ps;
static cmd_ln_t *config;

const godot_gdnative_core_api_struct *api = NULL;
const godot_gdnative_ext_nativescript_api_struct *nativescript_api = NULL;
const godot_variant godot_null;

GDCALLINGCONV void *stt_constructor(godot_object *p_instance, void *p_method_data);
GDCALLINGCONV void stt_destructor(godot_object *p_instance, void *p_method_data, void *p_user_data);
godot_variant stt_ready(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args);
godot_variant stt_start(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args);
godot_variant stt_stop(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args);
godot_variant stt_recognition_finished(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args);
godot_variant stt_get_result(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args);
void *run_recognition_thread(void *arg_ptr);
#ifndef _WIN32
int select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
#endif

void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *p_options)
{
  api = p_options->api_struct;
  for (int i = 0; i < api->num_extensions; i++)
  {
    switch (api->extensions[i]->type)
    {
    case GDNATIVE_EXT_NATIVESCRIPT:
    {
      nativescript_api = (godot_gdnative_ext_nativescript_api_struct *)api->extensions[i];
    };
    break;
    default:
      break;
    };
  };
}

void GDN_EXPORT godot_gdnative_terminate(godot_gdnative_terminate_options *p_options)
{
  api = NULL;
  nativescript_api = NULL;
}

void GDN_EXPORT godot_nativescript_init(void *p_handle)
{
  godot_instance_create_func create = {NULL, NULL, NULL};
  create.create_func = &stt_constructor;

  godot_instance_destroy_func destroy = {NULL, NULL, NULL};
  destroy.destroy_func = &stt_destructor;

  nativescript_api->godot_nativescript_register_class(p_handle, "STT", "Reference", create, destroy);

  godot_method_attributes attributes = {GODOT_METHOD_RPC_MODE_DISABLED};

  godot_instance_method ready = {NULL, NULL, NULL};
  ready.method = &stt_ready;

  godot_instance_method start = {NULL, NULL, NULL};
  start.method = &stt_start;

  godot_instance_method stop = {NULL, NULL, NULL};
  stop.method = &stt_stop;

  godot_instance_method recognition_finished = {NULL, NULL, NULL};
  recognition_finished.method = &stt_recognition_finished;

  godot_instance_method get_result = {NULL, NULL, NULL};
  get_result.method = &stt_get_result;

  nativescript_api->godot_nativescript_register_method(p_handle, "STT", "ready", attributes, ready);
  nativescript_api->godot_nativescript_register_method(p_handle, "STT", "start", attributes, start);
  nativescript_api->godot_nativescript_register_method(p_handle, "STT", "stop", attributes, stop);
  nativescript_api->godot_nativescript_register_method(p_handle, "STT", "recognition_finished", attributes, recognition_finished);
  nativescript_api->godot_nativescript_register_method(p_handle, "STT", "get_result", attributes, get_result);
}

GDCALLINGCONV void *stt_constructor(godot_object *p_instance, void *p_method_data)
{
  err_set_logfp(NULL);
#if defined(_WIN32)
  err_set_debug_level(0);
#endif
  status = starting;
  pthread_create(&recognition_thread, NULL, run_recognition_thread, NULL);
  printf("STT._construct()\n");
  return NULL;
}

GDCALLINGCONV void stt_destructor(godot_object *p_instance, void *p_method_data, void *p_user_data)
{
  status = self_destructing;
  pthread_join(recognition_thread, NULL);
  if (ad_close(ad) < 0) printf("Failed to close audio device\n");
  ps_free(ps);
  cmd_ln_free_r(config);
  printf("STT._destruct()\n");
}

void sleep_msec(int32 ms)
{
#if (defined(_WIN32) && !defined(GNUWINCE))
  Sleep(ms);
#else
  struct timeval tmo;
  tmo.tv_sec = 0;
  tmo.tv_usec = ms * 1000;
  select(0, NULL, NULL, NULL, &tmo);
#endif
}

void *run_recognition_thread(void *arg_ptr)
{
  config = cmd_ln_init(NULL, ps_args(), TRUE,
                       "-hmm", MODELDIR "/en-us/en-us",
                       "-lm", MODELDIR "/en-us/en-us.lm",
                       "-dict", MODELDIR "/en-us/cmudict-en-us.dict",
                       NULL);
  if (config == NULL) printf("Failed to create config object\n");
  ps = ps_init(config);
  if (ps == NULL) printf("Failed to create recognizer\n");
  if ((ad = ad_open()) == NULL) printf("Failed to open audio device\n");

  status = ready;
  printf("Speech to text module ready\n");
  while (status != self_destructing)
  {
    if (status == enabled)
    {
      printf("Recognition started\n");
      if (ad_start_rec(ad) < 0) printf("Failed to start recording\n");
      if (ps_start_utt(ps) < 0) printf("Failed to start utterance\n");
      
      // On Windows/MacOS we read until the user stops speaking and the buffer is empty 
      while (status == enabled || ad_n_samples != 0)
      {
        if ((ad_n_samples = ad_read(ad, adbuf, buffer_size)) < 0) printf("Failed to read audio\n");
        ps_process_raw(ps, adbuf, ad_n_samples, FALSE, FALSE);
      }
      
      if (ad_stop_rec(ad) < 0) printf("Failed to stop recording\n");
      if (ps_end_utt(ps) < 0) printf("Failed to end utterance\n");
      result = ps_get_hyp(ps, NULL);
      if (result == NULL) result = "";
      printf("Recognized text: %s\n", result);
      status = recognition_finished;
    }

    sleep_msec(100);
  }

  return NULL;
}

godot_variant stt_ready(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args)
{
  godot_variant ret_bool;
  api->godot_variant_new_bool(&ret_bool, status == ready);
  return ret_bool;
}

godot_variant stt_start(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args)
{
  status = enabled;
  return godot_null;
}

godot_variant stt_stop(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args)
{
  status = in_recognition;
  return godot_null;
}

godot_variant stt_recognition_finished(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args)
{
  godot_variant ret_bool;
  api->godot_variant_new_bool(&ret_bool, status == recognition_finished);
  return ret_bool;
}

godot_variant stt_get_result(godot_object *p_instance, void *p_method_data, void *p_user_data, int p_num_args, godot_variant **p_args)
{
  godot_string text_string;
  godot_variant ret_text;
  api->godot_string_new(&text_string);
  api->godot_string_parse_utf8(&text_string, result);
  api->godot_variant_new_string(&ret_text, &text_string);
  api->godot_string_destroy(&text_string);
  status = ready;
  return ret_text;
}