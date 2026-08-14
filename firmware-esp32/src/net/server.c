#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage/config.h"

static httpd_handle_t s_server;
static controller_t *s_ctrl;
static history_t *s_history;

// ----------------------------------------------------------------- WS state --
#define MAX_WS 4
static struct {
    httpd_handle_t hd;
    int fd;
} s_ws[MAX_WS];
static SemaphoreHandle_t s_ws_lock;

static void ws_add(httpd_handle_t hd, int fd)
{
    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_WS; i++) {
        if (!s_ws[i].hd) {
            s_ws[i].hd = hd;
            s_ws[i].fd = fd;
            break;
        }
    }
    xSemaphoreGive(s_ws_lock);
}

static void ws_remove(httpd_handle_t hd, int fd)
{
    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_WS; i++) {
        if (s_ws[i].hd == hd && s_ws[i].fd == fd) {
            s_ws[i].hd = NULL;
            s_ws[i].fd = -1;
            break;
        }
    }
    xSemaphoreGive(s_ws_lock);
}

// ---------------------------------------------------------------- web page --
static const char s_index_html[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Reflow Oven</title>"
    "<style>body{font-family:monospace;background:#111;color:#eee;padding:12px;max-width:640px;margin:0 auto}"
    ".big{font-size:44px}.dim{color:#888}button{background:#2a6fb0;color:#fff;border:0;padding:10px 14px;"
    "font-size:15px;margin:4px;border-radius:6px}#chart{width:100%;height:180px;background:#000}</style></head>"
    "<body><h2>Reflow Oven</h2>"
    "<div class=\"big\" id=\"temp\">--.- C</div>"
    "<div id=\"state\">IDLE</div><div id=\"meta\"></div>"
    "<button onclick=\"send('start')\">START</button>"
    "<button onclick=\"send('stop')\">STOP</button>"
    "<button onclick=\"send('ack')\">ACK FAULT</button>"
    "<div id=\"profs\"></div>"
    "<canvas id=\"chart\" width=\"600\" height=\"180\"></canvas>"
    "<script>"
    "var pts=[];"
    "function send(cmd,arg){fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({cmd:cmd,arg:arg})});}"
    "var ws=new WebSocket('ws://'+location.host+'/ws');"
    "ws.onmessage=function(e){var d=JSON.parse(e.data);pts.push([d.t,d.temp]);if(pts.length>600)pts.shift();draw();};"
    "function draw(){var c=document.getElementById('chart'),x=c.getContext('2d');x.clearRect(0,0,600,180);"
    "x.strokeStyle='#2a6fb0';x.beginPath();"
    "for(var i=0;i<pts.length;i++){var px=i/(pts.length-1)*600,py=180-(pts[i][1]/300)*180;"
    "i?x.lineTo(px,py):x.moveTo(px,py);}x.stroke();}"
    "function load(){fetch('/api/status').then(function(r){return r.json();}).then(function(d){"
    "document.getElementById('temp').textContent=d.temp.toFixed(1)+' C';"
    "document.getElementById('state').textContent=d.state+' '+d.phase;"
    "document.getElementById('meta').textContent='SP '+d.setpoint.toFixed(1)+'C  OUT '+d.duty+'%';"
    "var p=document.getElementById('profs');p.innerHTML='';"
    "d.profiles.forEach(function(n,i){var b=document.createElement('button');b.textContent=n+(i===d.selected?' *':'');"
    "b.onclick=function(){send('select',i);};p.appendChild(b);});});}"
    "setInterval(load,1000);load();"
    "</script></body></html>";

// ----------------------------------------------------------------- handlers --
static esp_err_t uri_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, s_index_html);
}

static esp_err_t uri_status(httpd_req_t *req)
{
    ctrl_state_t st = ctrl_state(s_ctrl);
    const char *stn = st == CTRL_STATE_RUN ? "RUN"
                     : st == CTRL_STATE_DONE ? "DONE"
                                             : "IDLE";
    safety_fault_t f = ctrl_fault(s_ctrl);

    char buf[512];
    int len = snprintf(buf, sizeof(buf),
                       "{\"temp\":%.1f,\"setpoint\":%.1f,\"duty\":%.0f,"
                       "\"state\":\"%s\",\"phase\":\"%s\",\"fault\":%d,\"selected\":%u,\"profiles\":[",
                       ctrl_last_temp(s_ctrl), ctrl_setpoint(s_ctrl), ctrl_duty(s_ctrl),
                       stn, ctrl_phase_name(s_ctrl), (int)f, config_selected());
    for (int i = 0; i < CONFIG_PROFILE_COUNT; i++) {
        len += snprintf(buf + len, sizeof(buf) - len, "%s\"%s\"", i ? "," : "", config_profile(i)->name);
    }
    snprintf(buf + len, sizeof(buf) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

static esp_err_t uri_control(httpd_req_t *req)
{
    char buf[64];
    int got = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (got < 0) {
        return ESP_FAIL;
    }
    buf[got] = '\0';

    if (strstr(buf, "\"start\"")) {
        ctrl_post(s_ctrl, CTRL_CMD_START, 0);
    } else if (strstr(buf, "\"stop\"")) {
        ctrl_post(s_ctrl, CTRL_CMD_STOP, 0);
    } else if (strstr(buf, "\"ack\"")) {
        ctrl_post(s_ctrl, CTRL_CMD_ACK_FAULT, 0);
    } else if (strstr(buf, "\"select\"")) {
        const char *ap = strstr(buf, "arg");
        int arg = 0;
        if (ap) {
            ap = strchr(ap, ':');
            if (ap) {
                arg = atoi(ap + 1);
            }
        }
        config_set_selected((uint8_t)arg);
        config_save();
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown cmd");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t uri_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0) {
            ws_add(req->handle, fd);
        }
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t r = httpd_ws_recv_frame(req, &frame, 0);
    if (r != ESP_OK || frame.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0) {
            ws_remove(req->handle, fd);
        }
    }
    return ESP_OK;
}

// ------------------------------------------------------------------- push ----
static void ws_push_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        char buf[64];
        float temp = ctrl_last_temp(s_ctrl);
        unsigned long t = 0;
        uint32_t n = history_count(s_history);
        if (n) {
            t = (unsigned long)history_get(s_history, n - 1)->t_ms;
        }
        snprintf(buf, sizeof(buf), "{\"t\":%lu,\"temp\":%.1f}", t, temp);

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)buf,
            .len = strlen(buf),
        };
        xSemaphoreTake(s_ws_lock, portMAX_DELAY);
        for (int i = 0; i < MAX_WS; i++) {
            if (s_ws[i].hd) {
                httpd_ws_send_frame_async(s_ws[i].hd, s_ws[i].fd, &frame);
            }
        }
        xSemaphoreGive(s_ws_lock);
    }
}

// ------------------------------------------------------------------- init ----
void server_init(controller_t *ctrl, history_t *hist)
{
    s_ctrl = ctrl;
    s_history = hist;
    for (int i = 0; i < MAX_WS; i++) {
        s_ws[i].fd = -1;
    }
    s_ws_lock = xSemaphoreCreateMutex();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        return;
    }

    httpd_uri_t u = { .method = HTTP_GET, .uri = "/", .handler = uri_index };
    httpd_register_uri_handler(s_server, &u);
    u.uri = "/api/status";
    u.handler = uri_status;
    httpd_register_uri_handler(s_server, &u);
    u.method = HTTP_POST;
    u.uri = "/api/control";
    u.handler = uri_control;
    httpd_register_uri_handler(s_server, &u);
    u.method = HTTP_GET;
    u.uri = "/ws";
    u.handler = uri_ws;
    u.is_websocket = true;
    httpd_register_uri_handler(s_server, &u);

    xTaskCreate(ws_push_task, "wspush", 4096, NULL, 4, NULL);
}