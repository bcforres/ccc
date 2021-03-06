/*
  Copyright (C) 2015 Bailey Forrest <baileycforrest@gmail.com>

  This file is part of CCC.

  CCC is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  CCC is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with CCC.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Interface for logger for handling exceptional events
 *
 * This is implemented as a singleton
 */

#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdbool.h>

#include "util/file_mark.h"
#include "util/status.h"

typedef enum log_type_t {
    LOG_ERR,  /**< Error */
    LOG_WARN, /**< Warning */
    LOG_NOTE  /**< Note */
} log_type_t;

/**
 * Name of current function
 *
 * NULL if none
 */
extern char *log_function;

/**
 * Initializes logger
 */
void logger_init(void);

/**
 * Log a message
 *
 * @param mark File mark to log from
 * @param type Type of log
 */
void logger_log(fmark_t *mark, log_type_t type, const char *fmt, ...);

/**
 * @return Returns true if an error message has been logged
 */
bool logger_has_error(void);

/**
 * @return Returns true if warning message has been logged
 */
bool logger_has_warn(void);

#endif /* _LOGGER_H_ */
