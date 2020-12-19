/***************************************************************************
 *   Copyright (C) 2020 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 * This software is available under either the Mozilla Public License      *
 * version 2.0 or the GNU LGPL version 2 (or later) license, whichever     *
 * you choose.                                                             *
 *                                                                         *
 * MPL 2.0:                                                                *
 *                                                                         *
 *   This Source Code Form is subject to the terms of the Mozilla Public   *
 *   License, v. 2.0. If a copy of the MPL was not distributed with this   *
 *   file, You can obtain one at http://mozilla.org/MPL/2.0/.              *
 *                                                                         *
 *                                                                         *
 * LGPL 2:                                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <platform.h>
#include <lib/libplctag.h>
#include <util/atomic_int.h>
#include <util/event_loop.h>
#include <util/debug.h>
#include <util/event_timer.h>
#include <util/socket.h>


static thread_p event_loop_thread = NULL;
static THREAD_FUNC(even_loop_handler);
static atomic_int library_shutdown = ATOMIC_INT_STATIC_INIT(0);


void event_loop_wake(void)
{
    pdebug(DEBUG_INFO, "Starting.");

    /* wake up the blocking socket event calls. */
    socket_event_wake();

    pdebug(DEBUG_INFO, "Done.");
}




int event_loop_init(void)
{
    pdebug(DEBUG_INFO, "Starting.");

    atomic_int_set(&library_shutdown, 0);

    rc = timer_event_init();
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to initialize event timer module, error %s!", plc_tag_decode_error(rc));
        return rc;
    }

    rc = socket_event_init();
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to initialize socket event module, error %s!", plc_tag_decode_error(rc));
        return rc;
    }

    /* create the platform thread. */
    rc = thread_create(&event_loop_thread, even_loop_handler, 32768, NULL);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to create event loop thread, error %s!", plc_tag_decode_error(rc));
        return rc;
    }

    pdebug(DEBUG_INFO, "Done.");

    return PLCTAG_STATUS_OK;
}


void event_loop_teardown(void)
{
    int rc = PLCTAG_STATUS_OK;

    pdebug(DEBUG_INFO, "Starting.");

    /* destroy the platform thread. */
    atomic_int_set(&library_shutdown, 1);

    rc = thread_join(event_loop_thread);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to join event loop thread, got error %s!", plc_tag_decode_error(rc));
        return;
    }

    rc = thread_destroy(&event_loop_thread);
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to destroy event loop thread!");
        return;
    }

    event_loop_thread = NULL;

    /* reset the shutdown flag to make sure that the library can restart. */
    atomic_int_set(&library_shutdown, 0);

    rc = socket_event_teardown();
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to tear down socket event module, got error %s!", plc_tag_decode_error(rc));
        return;
    }

    rc = timer_event_teardown();
    if(rc != PLCTAG_STATUS_OK) {
        pdebug(DEBUG_ERROR, "Unable to tear down socket event module, got error %s!", plc_tag_decode_error(rc));
        return;
    }

    pdebug(DEBUG_INFO, "Done.");
}



void event_loop_tickler(void)
{
    int64_t next_wake_time = INT64_MAX;
    int64_t current_time = time_ms();

    pdebug(DEBUG_DETAIL, "Starting.");

    /* call non-blocking ticklers first. */
    next_wake_time = timer_event_tickler(current_time);

    /* end with a blocking tickler */
    socket_event_tickler(next_wake_time, current_time);

    pdebug(DEBUG_DETAIL, "Done.");
}



THREAD_FUNC(even_loop_handler)
{
    (void)arg;

    pdebug(DEBUG_INFO, "Starting.");

    while(! atomic_int_get(&library_shutdown)) {
        event_loop_tickler();
    }

    pdebug(DEBUG_INFO, "Done.");

    THREAD_RETURN(0);
}
