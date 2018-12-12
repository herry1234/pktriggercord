/*
    pkTriggerCord
    Remote control of Pentax DSLR cameras.
    Copyright (C) 2011-2018 Andras Salamon <andras.salamon@melda.info>

    based on:

    pslr-shoot

    Command line remote control of Pentax DSLR cameras.
    Copyright (C) 2009 Ramiro Barreiro <ramiro_barreiro69@yahoo.es>
    With fragments of code from PK-Remote by Pontus Lidman.
    <https://sourceforge.net/projects/pkremote>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    and GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>
#include "pk_ctrl.grpc.pb.h"
extern "C" {
#include "pslr.h"
#include "pslr_lens.h"
}


using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using pkcamera::Field;
using pkcamera::CamStatus;
using pkcamera::PkCamera;
extern  bool debug;

double timeval_diff_sec(struct timeval *t2, struct timeval *t1) {
    //DPRINT("tv2 %ld %ld t1 %ld %ld\n", t2->tv_sec, t2->tv_usec, t1->tv_sec, t1->tv_usec);
    return (t2->tv_usec - t1->tv_usec) / 1000000.0 + (t2->tv_sec - t1->tv_sec);
}

void camera_close(pslr_handle_t camhandle) {
    pslr_disconnect(camhandle);
    pslr_shutdown(camhandle);
}

pslr_handle_t camera_connect( char *model, char *device, int timeout, char *error_message ) {
    struct timeval prev_time;
    struct timeval current_time;
    pslr_handle_t camhandle;
    int r;

    gettimeofday(&prev_time, NULL);
    while (!(camhandle = pslr_init( model, device ))) {
        gettimeofday(&current_time, NULL);
        DPRINT("diff: %f\n", timeval_diff_sec(&current_time, &prev_time));
        if ( timeout == 0 || timeout > timeval_diff_sec(&current_time, &prev_time)) {
            DPRINT("sleep 1 sec\n");
            sleep_sec(1);
        } else {
            snprintf(error_message, 1000, "%d %ds timeout exceeded\n", 1, timeout);
            return NULL;
        }
    }

    DPRINT("before connect\n");
    if (camhandle) {
        if ((r=pslr_connect(camhandle)) ) {
            if ( r != -1 ) {
                snprintf(error_message, 1000, "%d Cannot connect to Pentax camera. Please start the program as root.\n",1);
            } else {
                snprintf(error_message, 1000, "%d Unknown Pentax camera found.\n",1);
            }
            return NULL;
        }
    }
    return camhandle;
}

const char *is_string_prefix(const char *str, const char *prefix) {
    if ( !strncmp(str, prefix, strlen(prefix) ) ) {
        if ( strlen(str) <= strlen(prefix)+1 ) {
            return str;
        } else {
            return str+strlen(prefix)+1;
        }
    } else {
        return NULL;
    }
}


class PkCameraImpl final : public PkCamera::Service {
 public:
  explicit PkCameraImpl(const std::string& db) {
      _currentStatus = "Test";
  }

  bool check_camera(pslr_handle_t camhandle) {
    if ( !camhandle ) {
        return false;
    } else {
        return true;
    }
}

  Status GetCamStatus(ServerContext* context, const Field* field,
                    CamStatus* status) override {
            
            const char *client_message = &(field->name())[0u];
            const char *arg;
            char buf[2100];
            pslr_status pslrstatus;
            uint32_t iso = 0;
            uint32_t auto_iso_min = 0;
            uint32_t auto_iso_max = 0;
            char C;
            float F = 0;
            pslr_rational_t shutter_speed = {0, 0};
            DPRINT(":%s:\n",client_message);
            if ( !strcmp(client_message, "stopserver" ) ) {
                if ( camhandle ) {
                    camera_close(camhandle);
                }
                status->set_status(0);
                status->set_msg("END");
                exit(0);
            } else if ( !strcmp(client_message, "disconnect" ) ) {
                if ( camhandle ) {
                    camera_close(camhandle);
                }
                status->set_status(0);
                status->set_msg("disconnect");
            } else if ( (arg = is_string_prefix( client_message, "echo")) != NULL ) {
                sprintf( buf, "%.100s\n", arg);
                status->set_status(0);
                status->set_msg(buf);
            } else if (  (arg = is_string_prefix( client_message, "usleep")) != NULL ) {
                int microseconds = atoi(arg);
                usleep(microseconds);
                status->set_status(0);
                status->set_msg("Sleeping");
            } else if ( !strcmp(client_message, "connect") ) {
                if ( camhandle ) {
                    status->set_status(0);
                    status->set_msg("connected");
                } else if ( (camhandle = camera_connect( NULL, NULL, -1, buf ))  ) {
                    status->set_status(0);
                    status->set_msg("connected");
                } else {
                    status->set_status(1);
                    status->set_msg("NOT connected");
                }
            } else if ( !strcmp(client_message, "update_status") ) {
                if ( check_camera(camhandle) ) {
                    if ( !pslr_get_status(camhandle, &pslrstatus) ) {
                        status->set_status(0);
                    } else {
                        status->set_status(1);
                    }
                    status->set_msg("update status");
                }
            } else if ( !strcmp(client_message, "get_camera_name") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %s", 0, pslr_camera_name(camhandle));
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_lens_name") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%s", get_lens_name(pslrstatus.lens_id1, pslrstatus.lens_id2));
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_current_shutter_speed") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %d/%d", 0, pslrstatus.current_shutter_speed.nom, pslrstatus.current_shutter_speed.denom);
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_current_aperture") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%s", format_rational( pslrstatus.current_aperture, "%.1f"));
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_current_iso") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %d", 0, pslrstatus.current_iso);
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_bufmask") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %d", 0, pslrstatus.bufmask);
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_auto_bracket_mode") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %d", 0, pslrstatus.auto_bracket_mode);
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "get_auto_bracket_picture_count") ) {
                if ( check_camera(camhandle) ) {
                    sprintf(buf, "%d %d", 0, pslrstatus.auto_bracket_picture_count);
                    status->set_status(0);
                    status->set_msg(buf);
                }
            } else if ( !strcmp(client_message, "focus") ) {
                if ( check_camera(camhandle) ) {
                    pslr_focus(camhandle);
                    status->set_status(0);
                    status->set_msg("Focusing");
                }
            } else if ( !strcmp(client_message, "shutter") ) {
                if ( check_camera(camhandle) ) {
                    pslr_shutter(camhandle);
                    status->set_status(0);
                    status->set_msg("Shutter!");
                }
            } else if (  (arg = is_string_prefix( client_message, "delete_buffer")) != NULL ) {
                int bufno = atoi(arg);
                if ( check_camera(camhandle) ) {
                    pslr_delete_buffer(camhandle,bufno);
                    status->set_status(0);
                    status->set_msg("deleted buffer");
                }
            } else if (  (arg = is_string_prefix( client_message, "get_preview_buffer")) != NULL ) {
                int bufno = atoi(arg);
                if ( check_camera(camhandle) ) {
                    uint8_t *pImage;
                    uint32_t imageSize;
                    if ( pslr_get_buffer(camhandle, bufno, PSLR_BUF_PREVIEW, 4, &pImage, &imageSize) ) {
                        status->set_status(1);
                        sprintf(buf, "imageSize : %d", imageSize);
                        status->set_msg(buf);
                    } else {
                        sprintf(buf, "%d %d", 0, imageSize);
                        // write_socket_answer(buf);
                        // write_socket_answer_bin(pImage, imageSize);
                        status->set_status(0);
                        sprintf(buf, "TBD: %d", imageSize);
                        status->set_msg(buf);
                    }
                }
            } else if (  (arg = is_string_prefix( client_message, "get_buffer")) != NULL ) {
                int bufno = atoi(arg);
                if ( check_camera(camhandle) ) {
                    uint32_t imageSize;
                    if ( pslr_buffer_open(camhandle, bufno, PSLR_BUF_DNG, 0) ) {
                        // sprintf(buf, "%d\n", 1);
                        // write_socket_answer(buf);
                        status->set_status(1);
                        status->set_msg("can't open buffer");
                    } else {
                        imageSize = pslr_buffer_get_size(camhandle);
                        status->set_status(0);
                        status->set_msg("open buffer");
                        uint32_t current = 0;
                        while (1) {
                            uint32_t bytes;
                            uint8_t buf[65536];
                            bytes = pslr_buffer_read(camhandle, buf, sizeof (buf));
                            if (bytes == 0) {
                                break;
                            }
                            // write_socket_answer_bin( buf, bytes);
                            current += bytes;
                        }
                        pslr_buffer_close(camhandle);
                    }
                }
            } else if (  (arg = is_string_prefix( client_message, "set_shutter_speed")) != NULL ) {
                if ( check_camera(camhandle) ) {
                    // TODO: merge with pktriggercord-cli shutter speed parse
                    if (sscanf(arg, "1/%d%c", &shutter_speed.denom, &C) == 1) {
                        shutter_speed.nom = 1;
                        sprintf(buf, "%d %d %d\n", 0, shutter_speed.nom, shutter_speed.denom);
                    } else if ((sscanf(arg, "%f%c", &F, &C)) == 1) {
                        if (F < 2) {
                            F = F * 10;
                            shutter_speed.denom = 10;
                            shutter_speed.nom = F;
                        } else {
                            shutter_speed.denom = 1;
                            shutter_speed.nom = F;
                        }
                        sprintf(buf, "%d %d %d\n", 0, shutter_speed.nom, shutter_speed.denom);
                        status->set_status(0);
                    } else {
                        shutter_speed.nom = 0;
                        sprintf(buf,"1 Invalid shutter speed value.\n");
                        status->set_status(2);
                        
                    }
                    if (shutter_speed.nom) {
                        pslr_set_shutter(camhandle, shutter_speed);
                    }
                    status->set_msg(buf);
                }
            } else if (  (arg = is_string_prefix( client_message, "set_iso")) != NULL ) {
                if ( check_camera(camhandle) ) {
                    // TODO: merge with pktriggercord-cli shutter iso
                    if (sscanf(arg, "%d-%d%c", &auto_iso_min, &auto_iso_max, &C) != 2) {
                        auto_iso_min = 0;
                        auto_iso_max = 0;
                        iso = atoi(arg);
                    } else {
                        iso = 0;
                    }
                    if (iso==0 && auto_iso_min==0) {
                        sprintf(buf,"1 Invalid iso value.\n");
                        status->set_status(2);
                    } else {
                        pslr_set_iso(camhandle, iso, auto_iso_min, auto_iso_max);
                        sprintf(buf, "%d %d %d-%d\n", 0, iso, auto_iso_min, auto_iso_max);
                        status->set_status(0);
                    }
                    status->set_msg(buf);
                }
            } else {
                status->set_msg("INVALID CMD");
                status->set_status(101);
            }
            if(status->msg() == "") {
                status->set_msg("Camera Disconnected");
                status->set_status(101);
            }
            return Status::OK;
        }
 private:

  std::string _currentStatus;
  pslr_handle_t camhandle=NULL;
};
void RunServer() {
  std::string server_address("0.0.0.0:50051");
  PkCameraImpl service("anthing");

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}
int main(int argc, char **argv) {
    RunServer();
    return 0;
}
