/*
 * Copyright (C) 2011-2014 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef  __cplusplus
#define _GNU_SOURCE
#endif

#include <stdio.h>

#include "lsm_datatypes.hpp"

#include <libstoragemgmt/libstoragemgmt_accessgroups.h>
#include <libstoragemgmt/libstoragemgmt_common.h>
#include <libstoragemgmt/libstoragemgmt_disk.h>
#include <libstoragemgmt/libstoragemgmt_error.h>
#include <libstoragemgmt/libstoragemgmt_fs.h>
#include <libstoragemgmt/libstoragemgmt_initiators.h>
#include <libstoragemgmt/libstoragemgmt_nfsexport.h>
#include <libstoragemgmt/libstoragemgmt_optionaldata.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <libstoragemgmt/libstoragemgmt_pool.h>
#include <libstoragemgmt/libstoragemgmt_snapshot.h>
#include <libstoragemgmt/libstoragemgmt_systems.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_volumes.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <glib.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define LSM_DEFAULT_PLUGIN_DIR "/var/run/lsm/ipc"

int lsmStringListAppend(lsmStringList *sl, const char *value)
{
    int rc = LSM_ERR_INVALID_SL;

    if( LSM_IS_STRING_LIST(sl) ) {
        char *d = strdup(value);
        if(d) {
            g_ptr_array_add(sl->values, d);
            rc = LSM_ERR_OK;
        } else {
            rc = LSM_ERR_NO_MEMORY;
        }
    }
    return rc;
}

int lsmStringListDelete(lsmStringList *sl, uint32_t index)
{
    int rc = LSM_ERR_INVALID_SL;

    if( LSM_IS_STRING_LIST(sl) ) {
        if( index < sl->values->len ) {
            g_ptr_array_remove_index(sl->values, index);
            rc = LSM_ERR_OK;
        } else {
            rc = LSM_ERR_INDEX_BOUNDS;
        }
    }
    return rc;
}


int lsmStringListElemSet(lsmStringList *sl, uint32_t index,
                                            const char* value)
{
    int rc = LSM_ERR_OK;
    if( LSM_IS_STRING_LIST(sl) ) {
        if( index < sl->values->len ) {

            char *i = (char *)g_ptr_array_index(sl->values, index);

            if( i ) {
                free(i);
            }

            g_ptr_array_index(sl->values, index) = strdup(value);

            if( !g_ptr_array_index(sl->values, index) ) {
                rc = LSM_ERR_NO_MEMORY;
            }
        } else {
            g_ptr_array_set_size(sl->values, index + 1);
            g_ptr_array_index(sl->values, index) = strdup(value);

            if( !g_ptr_array_index(sl->values, index) ) {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } else {
        rc = LSM_ERR_INVALID_SL;
    }
    return rc;
}

const char *lsmStringListElemGet(lsmStringList *sl, uint32_t index)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        if( index < sl->values->len ) {
            return (const char*)g_ptr_array_index(sl->values, index);
        }
    }
    return NULL;
}

lsmStringList *lsmStringListAlloc(uint32_t size)
{
    lsmStringList *rc = NULL;

    rc = (lsmStringList *)malloc(sizeof(lsmStringList));
    if( rc ) {
        rc->magic = LSM_STRING_LIST_MAGIC;
        rc->values = g_ptr_array_sized_new(size);
        if( !rc->values ) {
            rc->magic = LSM_DEL_MAGIC(LSM_STRING_LIST_MAGIC);
            free(rc);
            rc = NULL;
        } else {
            g_ptr_array_set_size(rc->values, size);
            g_ptr_array_set_free_func(rc->values, free);
        }
    }

    return rc;
}

int lsmStringListFree(lsmStringList *sl)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        sl->magic = LSM_DEL_MAGIC(LSM_STRING_LIST_MAGIC);
        g_ptr_array_free(sl->values, TRUE);
        sl->values = NULL;
        free(sl);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_SL;
}

uint32_t lsmStringListSize(lsmStringList *sl)
{
    if( LSM_IS_STRING_LIST(sl) ) {
        return (uint32_t)sl->values->len;
    }
    return 0;
}

lsmStringList *lsmStringListCopy(lsmStringList *src)
{
    lsmStringList *dest = NULL;

    if( LSM_IS_STRING_LIST(src) ) {
        uint32_t size = lsmStringListSize(src);
        dest = lsmStringListAlloc(size);

        if( dest ) {
            uint32_t i;

            for( i = 0; i < size ; ++i ) {
                if ( LSM_ERR_OK != lsmStringListElemSet(dest, i,
                        lsmStringListElemGet(src, i))) {
                    /** We had an allocation failure setting an element item */
                    lsmStringListFree(dest);
                    dest = NULL;
                    break;
                }
            }
        }
    }
    return dest;
}

lsmConnect *getConnection()
{
    lsmConnect *c = (lsmConnect *)malloc(sizeof(lsmConnect));
    if (c) {
        memset(c, 0, sizeof(lsmConnect));
        c->magic = LSM_CONNECT_MAGIC;
    }
    return c;
}

void freeConnection(lsmConnect *c)
{
    if (LSM_IS_CONNECT(c)) {

        c->magic = LSM_DEL_MAGIC(LSM_CONNECT_MAGIC);
        c->flags = 0;

        if (c->uri) {
            xmlFreeURI(c->uri);
            c->uri = NULL;
        }

        if (c->error) {
            lsmErrorFree(c->error);
            c->error = NULL;
        }

        if( c->tp ) {
            delete(c->tp);
            c->tp = NULL;
        }

        if( c->raw_uri ) {
            free(c->raw_uri);
            c->raw_uri = NULL;
        }

        free(c);
    }
}

static int establishConnection( lsmConnect *c, const char * password,
                                uint32_t timeout, lsmErrorPtr *e,
                                lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    std::map<std::string, Value> params;

    try {
        params["uri"] = Value(c->raw_uri);

        if( password ) {
            params["password"] = Value(password);
        } else {
            params["password"] = Value();
        }
        params["timeout"] = Value(timeout);
        params["flags"] = Value(flags);
        Value p(params);

        c->tp->rpc("startup", p);
    } catch (const ValueException &ve) {
        *e = lsmErrorCreate(LSM_ERR_TRANSPORT_SERIALIZATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in serialization",
                                ve.what(), NULL, NULL, 0 );
        rc = LSM_ERR_TRANSPORT_SERIALIZATION;
    } catch (const LsmException &le) {
        *e = lsmErrorCreate(LSM_ERR_TRANSPORT_COMMUNICATION,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Error in communication",
                                le.what(), NULL, NULL, 0 );
        rc = LSM_ERR_TRANSPORT_COMMUNICATION;
    } catch (...) {
        *e = lsmErrorCreate(LSM_ERR_INTERNAL_ERROR,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Undefined exception",
                                NULL, NULL, NULL, 0 );
        rc = LSM_ERR_INTERNAL_ERROR;
    }
    return rc;
}

const char *uds_path(void)
{
    const char *plugin_dir = getenv("LSM_UDS_PATH");

    if (plugin_dir == NULL) {
        plugin_dir = LSM_DEFAULT_PLUGIN_DIR;
    }
    return plugin_dir;
}


int loadDriver(lsmConnect *c, const char *plugin_name, const char *password,
    uint32_t timeout, lsmErrorPtr *e, int startup, lsmFlag_t flags)
{
    int rc = LSM_ERR_OK;
    char *plugin_file = NULL;
    const char *plugin_dir = uds_path();

    if (asprintf(&plugin_file, "%s/%s", plugin_dir, plugin_name) == -1) {
        return LSM_ERR_NO_MEMORY;
    }

    if (access(plugin_file, R_OK) == 0) {
        int ec;
        int sd = Transport::getSocket(std::string(plugin_file), ec);

        if( sd >= 0 ) {
            c->tp = new Ipc(sd);
            if( startup ) {
                if( establishConnection(c, password, timeout, e, flags)) {
                    rc = LSM_ERR_PLUGIN_DLOPEN;
                }
            }
        } else {
             *e = lsmErrorCreate(LSM_ERR_PLUGIN_DLOPEN,
                                LSM_ERR_DOMAIN_FRAME_WORK,
                                LSM_ERR_LEVEL_ERROR, "Unable to connect to plugin",
                                NULL, dlerror(), NULL, 0 );

            rc = LSM_ERR_PLUGIN_DLOPEN;
        }
    } else {
        *e = lsmErrorCreate(LSM_ERR_PLUGIN_PERMISSIONS,
                            LSM_ERR_DOMAIN_FRAME_WORK,
                            LSM_ERR_LEVEL_ERROR, "Unable to access plugin",
                            NULL, NULL, NULL, 0 );

        rc = LSM_ERR_PLUGIN_PERMISSIONS;
    }

    free(plugin_file);
    return rc;
}

lsmErrorPtr lsmErrorCreate(lsmErrorNumber code, lsmErrorDomain domain,
    lsmErrorLevel level, const char* msg,
    const char *exception, const char *debug,
    const void *debug_data, uint32_t debug_data_size)
{
    lsmErrorPtr err = (lsmErrorPtr)malloc(sizeof(lsmError));

    if (err) {
        memset(err, 0, sizeof(lsmError));
        err->magic = LSM_ERROR_MAGIC;
        err->code = code;
        err->domain = domain;
        err->level = level;

        /* Any of these strdup calls could fail, but we will continue*/
        if (msg) {
            err->message = strdup(msg);
        }

        if (exception) {
            err->exception = strdup(exception);
        }

        if (debug) {
            err->debug = strdup(debug);
        }

        /* We are not going to fail the creation of the error if we cannot
         * allocate the storage for the debug data.
         */
        if (debug_data && (debug_data_size > 0)) {
            err->debug_data = malloc(debug_data_size);

            if (err->debug_data) {
                err->debug_data_size = debug_data_size;
                memcpy(err->debug_data, debug_data, debug_data_size);
            }
        }
    }
    return(lsmErrorPtr) err;
}

int lsmErrorFree(lsmErrorPtr e)
{
    if (!LSM_IS_ERROR(e)) {
        return LSM_ERR_INVALID_ERR;
    }

    if (e->debug_data) {
        free(e->debug_data);
        e->debug_data = NULL;
        e->debug_data_size = 0;
    }

    if (e->debug) {
        free(e->debug);
        e->debug = NULL;
    }

    if (e->exception) {
        free(e->exception);
        e->exception = NULL;
    }

    if (e->message) {
        free(e->message);
        e->message = NULL;
    }

    e->magic = LSM_DEL_MAGIC(LSM_ERROR_MAGIC);
    free(e);

    return LSM_ERR_OK;
}

#define LSM_RETURN_ERR_VAL(type_t, e, x, error) \
        if( LSM_IS_ERROR(e) ) {     \
            return e->x;            \
        }                           \
        return (type_t)error;               \


lsmErrorNumber lsmErrorNumberGet(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorNumber, e, code, -1);
}

lsmErrorDomain lsmErrorDomainGet(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorDomain, e, domain, -1);
}

lsmErrorLevel lsmErrorLevelGet(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(lsmErrorLevel, e, level, -1);
}

char* lsmErrorMessageGet( lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, message, NULL);
}

char* lsmErrorExceptionGet(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, exception, NULL);
}

char* lsmErrorDebugGet(lsmErrorPtr e)
{
    LSM_RETURN_ERR_VAL(char*, e, debug, NULL);
}

void* lsmErrorDebugDataGet(lsmErrorPtr e, uint32_t *size)
{
    if (LSM_IS_ERROR(e) && size != NULL) {
        if (e->debug_data) {
            *size = e->debug_data_size;
            return e->debug_data;
        } else {
            *size = 0;
        }
    }
    return NULL;
}

/**
 * When creating arrays of the different types the code is the same.  This
 * macro is used to create type safe code.
 * @param   name    Name of the function
 * @param   rtype   return type
 * @return An array of pointers of rtype
 */
#define CREATE_ALLOC_ARRAY_FUNC(name, rtype)\
rtype *name(uint32_t size)                  \
{                                           \
    rtype *rc = NULL;                       \
    if (size > 0) {                         \
        size_t s = sizeof(rtype) * size;    \
        rc = (rtype *) malloc(s);           \
    }                                       \
    return rc;                              \
}

/**
 * Common macro for freeing the memory associated with one of these
 * data structures.
 * @param name              Name of function to create
 * @param free_func         Function to call to free one of the elements
 * @param record_type       Type to record
 * @return None
 */
#define CREATE_FREE_ARRAY_FUNC(name, free_func, record_type, error)\
int name( record_type pa[], uint32_t size)                \
{                                                   \
    if (pa) {                                       \
        uint32_t i = 0;                             \
        for (i = 0; i < size; ++i) {                \
            free_func(pa[i]);                       \
        }                                           \
        free(pa);                                   \
        return LSM_ERR_OK;                          \
    }                                               \
    return error;                                   \
}


CREATE_ALLOC_ARRAY_FUNC(lsmPoolRecordArrayAlloc, lsmPool *)

lsmPool *lsmPoolRecordAlloc(const char *id, const char *name,
            uint64_t totalSpace, uint64_t freeSpace, uint64_t status,
            const char *system_id)
{
    lsmPool *rc = (lsmPool *)malloc(sizeof(lsmPool));
    if (rc) {
        memset(rc, 0, sizeof(lsmPool));
        rc->magic = LSM_POOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->totalSpace = totalSpace;
        rc->freeSpace = freeSpace;
        rc->status = status;
        rc->system_id = strdup(system_id);

        if( !rc->id || !rc->name || !rc->system_id ) {
            lsmPoolRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

void lsmPoolFreeSpaceSet(lsmPool *p, uint64_t free_space)
{
    if( LSM_IS_POOL(p) ) {
        p->freeSpace = free_space;
    }
}

lsmPool * lsmPoolRecordCopy( lsmPool *toBeCopied)
{
    if( LSM_IS_POOL(toBeCopied) ) {
        return lsmPoolRecordAlloc(toBeCopied->id, toBeCopied->name,
                                    toBeCopied->totalSpace,
                                    toBeCopied->freeSpace,
                                    toBeCopied->status,
                                    toBeCopied->system_id);
    }
    return NULL;
}

int lsmPoolRecordFree(lsmPool *p)
{
    if (LSM_IS_POOL(p)) {
        p->magic = LSM_DEL_MAGIC(LSM_POOL_MAGIC);
        if (p->name) {
            free(p->name);
            p->name = NULL;
        }

        if (p->id) {
            free(p->id);
            p->id = NULL;
        }

        if( p->system_id) {
            free(p->system_id);
            p->system_id = NULL;
        }
        free(p);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_POOL;
}

CREATE_FREE_ARRAY_FUNC(lsmPoolRecordArrayFree, lsmPoolRecordFree, lsmPool *,
                        LSM_ERR_INVALID_POOL)

char *lsmPoolNameGet(lsmPool *p)
{
    if (LSM_IS_POOL(p)) {
        return p->name;
    }
    return NULL;
}

char *lsmPoolIdGet(lsmPool *p)
{
    if (LSM_IS_POOL(p)) {
        return p->id;
    }
    return NULL;
}

uint64_t lsmPoolTotalSpaceGet(lsmPool *p)
{
    if (LSM_IS_POOL(p)) {
        return p->totalSpace;
    }
    return 0;
}

uint64_t lsmPoolFreeSpaceGet(lsmPool *p)
{
    if (LSM_IS_POOL(p)) {
        return p->freeSpace;
    }
    return 0;
}

uint64_t lsmPoolStatusGet( lsmPool *p )
{
    if (LSM_IS_POOL(p)) {
        return p->status;
    }
    return UINT64_MAX;
}

char *lsmPoolSystemIdGet( lsmPool *p )
{
    if (LSM_IS_POOL(p)) {
        return p->system_id;
    }
    return NULL;
}

CREATE_ALLOC_ARRAY_FUNC(lsmInitiatorRecordArrayAlloc, lsmInitiator *)

lsmInitiator *lsmInitiatorRecordAlloc(lsmInitiatorType idType, const char* id,
                                        const char* name)
{
    lsmInitiator *rc = (lsmInitiator *)malloc(sizeof(lsmInitiator));
    if (rc) {
        rc->magic = LSM_INIT_MAGIC;
        rc->idType = idType;
        rc->id = strdup(id);
        rc->name = strdup(name);

        if(!rc->id || !rc->name ) {
            lsmInitiatorRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsmInitiator *lsmInitiatorRecordCopy(lsmInitiator *i)
{
    lsmInitiator *rc = NULL;
    if( LSM_IS_INIT(i) ) {
        rc = lsmInitiatorRecordAlloc(i->idType, i->id, i->name);
    }
    return rc;
}

int lsmInitiatorRecordFree(lsmInitiator *i)
{
    if( LSM_IS_INIT(i) ) {
        i->magic = LSM_DEL_MAGIC(LSM_INIT_MAGIC);
        if (i->id) {
            free(i->id);
            i->id = NULL;
            free(i->name);
            i->name = NULL;
        }
        free(i);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_INIT;
}

CREATE_FREE_ARRAY_FUNC( lsmInitiatorRecordArrayFree, lsmInitiatorRecordFree,
                        lsmInitiator *, LSM_ERR_INVALID_INIT)

lsmInitiatorType lsmInitiatorTypeGet(lsmInitiator *i)
{
    if( LSM_IS_INIT(i) ) {
        return i->idType;
    }
    return LSM_INITIATOR_OTHER;
}

char *lsmInitiatorIdGet(lsmInitiator *i)
{
    if( LSM_IS_INIT(i) ) {
        return i->id;
    }
    return NULL;
}

char *lsmInitiatorNameGet(lsmInitiator *i)
{
    if( LSM_IS_INIT(i) ) {
        return i->name;
    }
    return NULL;
}

CREATE_ALLOC_ARRAY_FUNC(lsmVolumeRecordArrayAlloc, lsmVolume *)

lsmVolume * lsmVolumeRecordAlloc(const char *id, const char *name,
    const char *vpd83, uint64_t blockSize,
    uint64_t numberOfBlocks,
    uint32_t status, const char *system_id, const char *pool_id)
{
    lsmVolume *rc = (lsmVolume *)malloc(sizeof(lsmVolume));
    if (rc) {
        rc->magic = LSM_VOL_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->vpd83 = strdup(vpd83);
        rc->blockSize = blockSize;
        rc->numberOfBlocks = numberOfBlocks;
        rc->status = status;
        rc->system_id = strdup(system_id);
        rc->pool_id = strdup(pool_id);

        if( !rc->id || !rc->name || !rc->vpd83 || !rc->system_id ||
            !rc->pool_id) {
            lsmVolumeRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsmDiskRecordArrayAlloc, lsmDisk *)

lsmDisk *lsmDiskRecordAlloc(const char *id, const char *name,
        lsmDiskType disk_type, uint64_t block_size, uint64_t block_count,
        uint64_t disk_status, lsmOptionalData *op, const char *system_id)
{
    lsmDisk *rc = (lsmDisk *)malloc(sizeof(lsmDisk));
    if( rc ) {
        rc->magic = LSM_DISK_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->disk_type = disk_type;
        rc->block_size = block_size;
        rc->block_count = block_count;
        rc->disk_status = disk_status;
        rc->optional_data = lsmOptionalDataRecordCopy(op);
        rc->system_id = strdup(system_id);

        if( !rc->id || !rc->name || !rc->system_id ) {
            lsmDiskRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

CREATE_ALLOC_ARRAY_FUNC(lsmSystemRecordArrayAlloc, lsmSystem *)

lsmSystem *lsmSystemRecordAlloc( const char *id, const char *name,
                                    uint32_t status)
{
    lsmSystem *rc = (lsmSystem *)malloc(sizeof(lsmSystem));
    if (rc) {
        rc->magic = LSM_SYSTEM_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->status = status;
        if( !rc->name || !rc->id ) {
            lsmSystemRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsmSystemRecordFree(lsmSystem *s)
{
    if( LSM_IS_SYSTEM(s) ) {
        s->magic = LSM_DEL_MAGIC(LSM_SYSTEM_MAGIC);

        if (s->id) {
            free(s->id);
            s->id = NULL;
        }

        if (s->name) {
            free(s->name);
            s->name = NULL;
        }

        free(s);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_SYSTEM;
}

CREATE_FREE_ARRAY_FUNC(lsmSystemRecordArrayFree, lsmSystemRecordFree,
                        lsmSystem *, LSM_ERR_INVALID_SYSTEM)

lsmSystem *lsmSystemRecordCopy(lsmSystem *s)
{
    lsmSystem *rc = NULL;
    if( LSM_IS_SYSTEM(s) ) {
        rc = lsmSystemRecordAlloc(s->id, s->name, s->status);
    }
    return rc;
}

const char *lsmSystemIdGet(lsmSystem *s)
{
    if( LSM_IS_SYSTEM(s) ) {
        return s->id;
    }
    return NULL;
}

const char *lsmSystemNameGet(lsmSystem *s)
{
    if( LSM_IS_SYSTEM(s) ) {
        return s->name;
    }
    return NULL;
}

uint32_t lsmSystemStatusGet(lsmSystem *s)
{
    if( LSM_IS_SYSTEM(s) ) {
        return s->status;
    }
    return UINT32_MAX;
}

lsmVolume *lsmVolumeRecordCopy(lsmVolume *vol)
{
    lsmVolume *rc = NULL;
    if( LSM_IS_VOL(vol) ) {
        rc = lsmVolumeRecordAlloc(vol->id, vol->name, vol->vpd83,
                                  vol->blockSize, vol->numberOfBlocks,
                                  vol->status, vol->system_id, vol->pool_id);
    }
    return rc;
}

int lsmVolumeRecordFree(lsmVolume *v)
{
    if ( LSM_IS_VOL(v) ) {
        v->magic = LSM_DEL_MAGIC(LSM_VOL_MAGIC);

        if (v->id) {
            free(v->id);
            v->id = NULL;
        }

        if (v->name) {
            free(v->name);
            v->name = NULL;
        }

        if (v->vpd83) {
            free(v->vpd83);
            v->vpd83 = NULL;
        }

        if( v->system_id ) {
            free(v->system_id);
            v->system_id = NULL;
        }

        if( v->pool_id ) {
            free(v->pool_id);
            v->pool_id = NULL;
        }

        free(v);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_VOL;
}

CREATE_FREE_ARRAY_FUNC( lsmVolumeRecordArrayFree, lsmVolumeRecordFree,
                        lsmVolume *, LSM_ERR_INVALID_VOL)

lsmDisk *lsmDiskRecordCopy(lsmDisk *disk)
{
    if( LSM_IS_DISK(disk) ) {
        return lsmDiskRecordAlloc(disk->id, disk->name, disk->disk_type,
                    disk->block_size, disk->block_count, disk->disk_status,
                    disk->optional_data, disk->system_id);
    }
    return NULL;
}

int lsmDiskRecordFree(lsmDisk *d)
{
    if ( LSM_IS_DISK(d) ) {
        d->magic = LSM_DEL_MAGIC(LSM_DISK_MAGIC);

        free(d->id);
        d->id = NULL;

        free(d->name);
        d->name = NULL;

        free(d->system_id);
        d->system_id = NULL;

        lsmOptionalDataRecordFree(d->optional_data);
        d->optional_data = NULL;

        free(d);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_DISK;
}

CREATE_FREE_ARRAY_FUNC( lsmDiskRecordArrayFree, lsmDiskRecordFree,
                        lsmDisk *, LSM_ERR_INVALID_DISK)

/* We would certainly expand this to encompass the entire function */
#define MEMBER_GET(x, validation, member, error)  \
    if( validation(x) ) {   \
        return x->member;   \
    } else {                \
        return error;       \
    }

/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_REF(x, validation, member, value, alloc_func, free_func, error)  \
    if( validation(x) ) {                   \
        if(x->member) {                     \
            free_func(x->member);           \
            x->member = NULL;               \
        }                                   \
        if( value ) {                       \
            x->member = alloc_func(value);  \
            if( !x->member ) {              \
                return LSM_ERR_NO_MEMORY;   \
            }                               \
        }                                   \
        return LSM_ERR_OK;                  \
    } else {                                \
        return error;                       \
    }

/* We would certainly expand this to encompass the entire function */
#define MEMBER_SET_VAL(x, validation, member, value, error)  \
    if( validation(x) ) {                   \
        x->member = value;                  \
        return LSM_ERR_OK;                  \
    } else {                                \
        return error;                       \
    }

const char* lsmVolumeIdGet(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, id, NULL);
}

const char* lsmVolumeNameGet(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, name, NULL);
}

const char* lsmVolumeVpd83Get(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, vpd83, NULL);
}

uint64_t lsmVolumeBlockSizeGet(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, blockSize, 0);
}

uint64_t lsmVolumeNumberOfBlocksGet(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, numberOfBlocks, 0);
}

uint32_t lsmVolumeOpStatusGet(lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, status, 0);
}

char *lsmVolumeSystemIdGet( lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, system_id, NULL);
}

char *lsmVolumePoolIdGet( lsmVolume *v)
{
    MEMBER_GET(v, LSM_IS_VOL, pool_id, NULL);
}

const char *lsmDiskIdGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, id, NULL);
}

const char *lsmDiskNameGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, name, NULL);
}

lsmDiskType lsmDiskTypeGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, disk_type, LSM_DISK_TYPE_OTHER);
}

uint64_t lsmDiskBlockSizeGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, block_size, 0);
}

uint64_t lsmDiskNumberOfBlocksGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, block_count, 0);
}

uint64_t lsmDiskStatusGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, disk_status, LSM_DISK_STATUS_UNKNOWN);
}

const char *lsmDiskSystemIdGet( lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, system_id, NULL);
}

lsmOptionalData *lsmDiskOptionalDataGet(lsmDisk *d)
{
    MEMBER_GET(d, LSM_IS_DISK, optional_data, NULL);
}

CREATE_ALLOC_ARRAY_FUNC(lsmAccessGroupRecordArrayAlloc, lsmAccessGroup *)

lsmAccessGroup *lsmAccessGroupRecordAlloc(const char *id,
                                        const char *name,
                                        lsmStringList *initiators,
                                        const char *system_id)
{
    lsmAccessGroup *rc = NULL;
    if( id && name && system_id ) {
        rc = (lsmAccessGroup *)malloc(sizeof(lsmAccessGroup));
        if( rc ) {
            rc->magic = LSM_ACCESS_GROUP_MAGIC;
            rc->id = strdup(id);
            rc->name = strdup(name);
            rc->system_id = strdup(system_id);
            rc->initiators = lsmStringListCopy(initiators);

            if( !rc->id || !rc->name || !rc->system_id ) {
                lsmAccessGroupRecordFree(rc);
                rc = NULL;
            }
        }
    }
    return rc;
}

lsmAccessGroup *lsmAccessGroupRecordCopy( lsmAccessGroup *ag )
{
    lsmAccessGroup *rc = NULL;
    if( LSM_IS_ACCESS_GROUP(ag) ) {
        rc = lsmAccessGroupRecordAlloc(ag->id, ag->name,
                                           ag->initiators, ag->system_id);
    }
    return rc;
}

int lsmAccessGroupRecordFree(lsmAccessGroup *ag)
{
    if( LSM_IS_ACCESS_GROUP(ag) ) {
        ag->magic = LSM_DEL_MAGIC(LSM_ACCESS_GROUP_MAGIC);
        free(ag->id);
        free(ag->name);
        free(ag->system_id);
        lsmStringListFree(ag->initiators);
        free(ag);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_ACCESS_GROUP;
}

CREATE_FREE_ARRAY_FUNC(lsmAccessGroupRecordArrayFree, lsmAccessGroupRecordFree,
                        lsmAccessGroup *, LSM_ERR_INVALID_ACCESS_GROUP)

const char *lsmAccessGroupIdGet( lsmAccessGroup *group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->id;
    }
    return NULL;
}
const char *lsmAccessGroupNameGet( lsmAccessGroup *group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->name;
    }
    return NULL;
}
const char *lsmAccessGroupSystemIdGet( lsmAccessGroup *group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->system_id;
    }
    return NULL;
}

lsmStringList * lsmAccessGroupInitiatorIdGet( lsmAccessGroup *group )
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        return group->initiators;
    }
    return NULL;
}

void lsmAccessGroupInitiatorIdSet( lsmAccessGroup *group, lsmStringList *il)
{
    if( LSM_IS_ACCESS_GROUP(group) ) {
        if( group->initiators && group->initiators != il ) {
            lsmStringListFree(group->initiators);
        }

        group->initiators = il;
    }
}

lsmErrorPtr lsmErrorLastGet(lsmConnect *c)
{
    if( LSM_IS_CONNECT(c) ) {
        lsmErrorPtr e = c->error;
        c->error = NULL;
        return e;
    }
    return NULL;
}

lsmBlockRange *lsmBlockRangeRecordAlloc(uint64_t source_start,
                                            uint64_t dest_start,
                                            uint64_t block_count)
{
    lsmBlockRange *rc = NULL;

    rc = (lsmBlockRange*) malloc(sizeof(lsmBlockRange));
    if( rc ) {
        rc->magic = LSM_BLOCK_RANGE_MAGIC;
        rc->source_start = source_start;
        rc->dest_start = dest_start;
        rc->block_count = block_count;
    }
    return rc;
}

int  lsmBlockRangeRecordFree( lsmBlockRange *br )
{
    if( LSM_IS_BLOCK_RANGE(br) ) {
        br->magic = LSM_DEL_MAGIC(LSM_BLOCK_RANGE_MAGIC);
        free(br);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_BLOCK_RANGE;
}

lsmBlockRange *lsmBlockRangeRecordCopy( lsmBlockRange *source )
{
    lsmBlockRange *dest = NULL;

    if( LSM_IS_BLOCK_RANGE(source) ) {
        dest = lsmBlockRangeRecordAlloc(source->source_start,
                                        source->dest_start,
                                        source->block_count);
    }
    return dest;
}

CREATE_ALLOC_ARRAY_FUNC(lsmBlockRangeRecordArrayAlloc, lsmBlockRange *)
CREATE_FREE_ARRAY_FUNC(lsmBlockRangeRecordArrayFree, lsmBlockRangeRecordFree,
                        lsmBlockRange *, LSM_ERR_INVALID_BLOCK_RANGE)


uint64_t lsmBlockRangeSourceStartGet(lsmBlockRange *br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, source_start, 0);
}

uint64_t lsmBlockRangeDestStartGet(lsmBlockRange *br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, dest_start, 0);
}

uint64_t lsmBlockRangeBlockCountGet(lsmBlockRange *br)
{
    MEMBER_GET(br, LSM_IS_BLOCK_RANGE, block_count, 0);
}

lsmFs * lsmFsRecordAlloc( const char *id, const char *name,
                                            uint64_t total_space,
                                            uint64_t free_space,
                                            const char *pool_id,
                                            const char *system_id)
{
    lsmFs *rc = NULL;
    rc = (lsmFs *)malloc(sizeof(lsmFs));
    if( rc ) {
        rc->magic = LSM_FS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->pool_id = strdup(pool_id);
        rc->system_id = strdup(system_id);
        rc->total_space = total_space;
        rc->free_space = free_space;

        if( !rc->id || !rc->name || !rc->pool_id || !rc->system_id ) {
            lsmFsRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsmFsRecordFree( lsmFs *fs)
{
    if( LSM_IS_FS(fs) ) {
        fs->magic = LSM_DEL_MAGIC(LSM_FS_MAGIC);
        free(fs->id);
        free(fs->name);
        free(fs->pool_id);
        free(fs->system_id);
        free(fs);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_FS;
}

lsmFs *lsmFsRecordCopy(lsmFs *source)
{
    lsmFs *dest = NULL;

    if( LSM_IS_FS(source) ) {
        dest = lsmFsRecordAlloc(source->id, source->name,
                                source->total_space, source->free_space,
                                source->pool_id,
                                source->system_id);
    }
    return dest;
}

CREATE_ALLOC_ARRAY_FUNC(lsmFsRecordArrayAlloc, lsmFs *)
CREATE_FREE_ARRAY_FUNC(lsmFsRecordArrayFree, lsmFsRecordFree, lsmFs *,
                        LSM_ERR_INVALID_FS)

const char *lsmFsIdGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, id, NULL);
}

const char *lsmFsNameGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, name, NULL);
}

const char *lsmFsSystemIdGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, system_id, NULL);
}

const char *lsmFsPoolIdGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, pool_id, NULL);
}

uint64_t lsmFsTotalSpaceGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, total_space, 0);
}
uint64_t lsmFsFreeSpaceGet(lsmFs *fs)
{
    MEMBER_GET(fs, LSM_IS_FS, free_space, 0);
}

lsmSs * lsmSsRecordAlloc( const char *id, const char *name,
                                            uint64_t ts)
{
    lsmSs *rc = (lsmSs *) malloc(sizeof(lsmSs));
    if( rc ) {
        rc->magic = LSM_SS_MAGIC;
        rc->id = strdup(id);
        rc->name = strdup(name);
        rc->ts = ts;

        if( !rc->id || ! rc->name ) {
            lsmSsRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsmSs *lsmSsRecordCopy(lsmSs *source)
{
    lsmSs *rc = NULL;
    if( LSM_IS_SS(source) ) {
        rc = lsmSsRecordAlloc(  lsmSsIdGet(source),
                                lsmSsNameGet(source),
                                lsmSsTimeStampGet(source));
    }
    return rc;
}

int lsmSsRecordFree( lsmSs *ss)
{
    if( LSM_IS_SS(ss) ) {
        ss->magic = LSM_DEL_MAGIC(LSM_SS_MAGIC);
        free(ss->id);
        free(ss->name);
        free(ss);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_SS;
}

CREATE_ALLOC_ARRAY_FUNC(lsmSsRecordArrayAlloc, lsmSs *)
CREATE_FREE_ARRAY_FUNC(lsmSsRecordArrayFree, lsmSsRecordFree, lsmSs *,
                        LSM_ERR_INVALID_SS)

const char *lsmSsIdGet(lsmSs *ss)
{
    MEMBER_GET(ss, LSM_IS_SS, id, NULL);
}

const char *lsmSsNameGet(lsmSs *ss)
{
    MEMBER_GET(ss, LSM_IS_SS, name, NULL);
}

uint64_t lsmSsTimeStampGet(lsmSs *ss)
{
    MEMBER_GET(ss, LSM_IS_SS, ts, 0);
}

lsmNfsExport *lsmNfsExportRecordAlloc( const char *id,
                                            const char *fs_id,
                                            const char *export_path,
                                            const char *auth,
                                            lsmStringList *root,
                                            lsmStringList *rw,
                                            lsmStringList *ro,
                                            uint64_t anonuid,
                                            uint64_t anongid,
                                            const char *options)
{
     lsmNfsExport *rc = NULL;

    /* This is required */
    if( fs_id ) {
        rc = (lsmNfsExport *)malloc(sizeof(lsmNfsExport));
        if( rc ) {
            rc->magic = LSM_NFS_EXPORT_MAGIC;
            rc->id = (id) ? strdup(id) : NULL;
            rc->fs_id = strdup(fs_id);
            rc->export_path = (export_path) ? strdup(export_path) : NULL;
            rc->auth_type = (auth) ? strdup(auth) : NULL;
            rc->root = lsmStringListCopy(root);
            rc->rw = lsmStringListCopy(rw);
            rc->ro = lsmStringListCopy(ro);
            rc->anonuid = anonuid;
            rc->anongid = anongid;
            rc->options = (options) ? strdup(options) : NULL;

            if( !rc->fs_id ) {
                lsmNfsExportRecordFree(rc);
                rc = NULL;
            }
        }
    }

    return rc;
}

int lsmNfsExportRecordFree( lsmNfsExport *exp )
{
    if( LSM_IS_NFS_EXPORT(exp) ) {
        exp->magic = LSM_DEL_MAGIC(LSM_NFS_EXPORT_MAGIC);
        free(exp->id);
        free(exp->fs_id);
        free(exp->export_path);
        free(exp->auth_type);
        lsmStringListFree(exp->root);
        lsmStringListFree(exp->rw);
        lsmStringListFree(exp->ro);
        free(exp->options);

        free(exp);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_NFS;
}


lsmNfsExport *lsmNfsExportRecordCopy( lsmNfsExport *s )
{
    if(LSM_IS_NFS_EXPORT(s)) {
        return lsmNfsExportRecordAlloc(s->id, s->fs_id, s->export_path,
                                s->auth_type, s->root, s->rw, s->ro, s->anonuid,
                                s->anongid, s->options);
    }
    return NULL;
}

CREATE_ALLOC_ARRAY_FUNC(lsmNfsExportRecordArrayAlloc, lsmNfsExport *)
CREATE_FREE_ARRAY_FUNC(lsmNfsExportRecordArrayFree, lsmNfsExportRecordFree,
                        lsmNfsExport *, LSM_ERR_INVALID_NFS)

const char *lsmNfsExportIdGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, id, NULL);
}

int lsmNfsExportIdSet(lsmNfsExport *exp, const char *ep )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, id, ep, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportFsIdGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, fs_id, NULL);
}

int lsmNfsExportFsIdSet( lsmNfsExport *exp, const char *fs_id)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, fs_id, fs_id, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportExportPathGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, export_path, NULL);
}

int lsmNfsExportExportPathSet( lsmNfsExport *exp, const char *ep )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, export_path, ep, strdup, free,
                LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportAuthTypeGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, auth_type, NULL);
}

int lsmNfsExportAuthTypeSet( lsmNfsExport *exp, const char *auth )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, auth_type, auth, strdup, free,
                LSM_ERR_INVALID_NFS);
}

lsmStringList * lsmNfsExportRootGet( lsmNfsExport *exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, root, NULL);
}

int lsmNfsExportRootSet( lsmNfsExport *exp, lsmStringList *root)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, root, root, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

lsmStringList * lsmNfsExportReadWriteGet( lsmNfsExport *exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, rw, NULL);
}

int lsmNfsExportReadWriteSet( lsmNfsExport *exp, lsmStringList *rw)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, rw, rw, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

lsmStringList * lsmNfsExportReadOnlyGet( lsmNfsExport *exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, ro, NULL);
}

int lsmNfsExportReadOnlySet(lsmNfsExport *exp, lsmStringList *ro)
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, ro, ro, lsmStringListCopy,
                lsmStringListFree, LSM_ERR_INVALID_NFS);
}

uint64_t lsmNfsExportAnonUidGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, anonuid, ANON_UID_GID_ERROR);
}

int lsmNfsExportAnonUidSet( lsmNfsExport *exp, uint64_t value)
{
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anonuid, value,
                    LSM_ERR_INVALID_NFS);
}

uint64_t lsmNfsExportAnonGidGet( lsmNfsExport *exp )
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, anongid, ANON_UID_GID_ERROR);
}

int lsmNfsExportAnonGidSet( lsmNfsExport *exp, uint64_t value )
{
    MEMBER_SET_VAL(exp, LSM_IS_NFS_EXPORT, anongid, value,
                    LSM_ERR_INVALID_NFS);
}

const char *lsmNfsExportOptionsGet( lsmNfsExport *exp)
{
    MEMBER_GET(exp, LSM_IS_NFS_EXPORT, options, NULL);
}

int lsmNfsExportOptionsSet( lsmNfsExport *exp, const char *value )
{
    MEMBER_SET_REF(exp, LSM_IS_NFS_EXPORT, options, value, strdup, free,
                LSM_ERR_INVALID_NFS);
}

lsmCapabilityValueType lsmCapabilityGet(lsmStorageCapabilities *cap,
                                        lsmCapabilityType t)
{
    lsmCapabilityValueType rc = LSM_CAPABILITY_UNKNOWN;

    if( LSM_IS_CAPABILITIY(cap) && (uint32_t)t < cap->len ) {
        rc = (lsmCapabilityValueType)cap->cap[t];
    }
    return rc;
}

int lsmCapabilitySet(lsmStorageCapabilities *cap, lsmCapabilityType t,
                        lsmCapabilityValueType v)
{
    int rc = LSM_ERR_INVALID_ARGUMENT;

    if( !LSM_IS_CAPABILITIY(cap) ) {
        return LSM_ERR_INVALID_CAPABILITY;
    }

    if( (uint32_t)t < cap->len) {
        cap->cap[t] = v;
        rc = LSM_ERR_OK;
    }
    return rc;
}

int lsmCapabilitySetN( lsmStorageCapabilities *cap,
                                lsmCapabilityValueType v, uint32_t number,
                                ... )
{
    uint32_t i = 0;
    int rc = LSM_ERR_OK;

    if( !LSM_IS_CAPABILITIY(cap) ) {
        return LSM_ERR_INVALID_CAPABILITY;
    }

    if( number ) {
        va_list var_arg;

        va_start(var_arg, number);

        for( i = 0; i < number; ++i ) {
            int index = va_arg(var_arg, int);

            if( index < (int)cap->len ) {
                cap->cap[index] = v;
            } else {
                rc = LSM_ERR_INVALID_ARGUMENT;
                break;
            }
        }

        va_end(var_arg);
    }

    return rc;
}

static char* bytesToString(uint8_t *a, uint32_t len)
{
    char *buff = NULL;

    if( a && len ) {
        uint32_t i = 0;
        char *tmp = NULL;
        size_t str_len = ((sizeof(char) * 2) * len + 1);
        buff = (char*)malloc(str_len);

        if( buff ) {
            tmp = buff;
            for( i = 0; i < len; ++i ) {
                tmp += sprintf(tmp, "%02x", a[i]);
            }
            buff[str_len - 1] = '\0';
        }
    }
    return buff;
}

static uint8_t *stringToBytes(const char *hex_string, uint32_t *l)
{
    uint8_t *rc = NULL;

    if( hex_string && l ) {
        size_t len = strlen(hex_string);
        if( len && (len % 2) == 0) {
            len /= 2;
            rc = (uint8_t*)malloc( sizeof(uint8_t) * len);
            if( rc ) {
                size_t i;
                const char *t = hex_string;
                *l = len;

                for( i = 0; i < len; ++i ) {
                    if( 1 != sscanf(t, "%02hhx", &rc[i])) {
                        free(rc);
                        rc = NULL;
                        *l = 0;
                        break;
                    }
                    t += 2;
                }
            }
        }
    }
    return rc;
}


lsmStorageCapabilities *lsmCapabilityRecordAlloc(const char *value)
{
    lsmStorageCapabilities *rc = NULL;
    rc = (lsmStorageCapabilities *)malloc(sizeof(struct _lsmStorageCapabilities));
    if(rc) {
        rc->magic = LSM_CAPABILITIES_MAGIC;

        if( value ) {
            rc->cap = stringToBytes(value, &rc->len);
        } else {
            rc->cap = (uint8_t *)malloc(sizeof(uint8_t) * LSM_CAP_MAX);
            if( rc->cap ) {
                rc->len = LSM_CAP_MAX;
                memset(rc->cap, 0, sizeof(uint8_t) * LSM_CAP_MAX);
            }
        }

        if( !rc->cap ) {
            lsmCapabilityRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

int lsmCapabilityRecordFree(lsmStorageCapabilities *cap)
{
    if( LSM_IS_CAPABILITIY(cap) ) {
        cap->magic = LSM_DEL_MAGIC(LSM_CAPABILITIES_MAGIC);
        free(cap->cap);
        free(cap);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_CAPABILITY;
}

char* capabilityString(lsmStorageCapabilities *c)
{
    char *rc = NULL;
    if( LSM_IS_CAPABILITIY(c) ) {
        rc = bytesToString(c->cap, c->len);
    }
    return rc;
}

lsmOptionalData *lsmOptionalDataRecordAlloc(void)
{
    lsmOptionalData *rc = NULL;

    rc = (lsmOptionalData *)malloc(sizeof(lsmOptionalData));
    if( rc ) {
        rc->magic = LSM_OPTIONAL_DATA_MAGIC;
        rc->data = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);
        if ( !rc->data ) {
            lsmOptionalDataRecordFree(rc);
            rc = NULL;
        }
    }
    return rc;
}

lsmOptionalData *lsmOptionalDataRecordCopy(lsmOptionalData *src)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    lsmOptionalData *dest = NULL;
    if( LSM_IS_OPTIONAL_DATA(src) ) {
        dest = lsmOptionalDataRecordAlloc();
        if( dest ) {
            /* Walk through each from src and duplicate it to dest*/
            g_hash_table_iter_init(&iter, src->data);
            while(g_hash_table_iter_next(&iter, &key, &value)) {
                if( LSM_ERR_OK != lsmOptionalDataStringSet(dest,
                                    (const char*)key, (const char*)value))
                {
                    lsmOptionalDataRecordFree(dest);
                    dest = NULL;
                }
            }
        }
    }
    return dest;
}

int lsmOptionalDataRecordFree(lsmOptionalData *op)
{
    if( LSM_IS_OPTIONAL_DATA(op) ) {
        op->magic = LSM_DEL_MAGIC(LSM_OPTIONAL_DATA_MAGIC);

        if( op->data ) {
            g_hash_table_destroy(op->data);
        }

        free(op);
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_OPTIONAL_DATA;
}

int lsmOptionalDataListGet(lsmOptionalData *op, lsmStringList **l,
                            uint32_t *count)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    if( LSM_IS_OPTIONAL_DATA(op) ) {
        *count = g_hash_table_size(op->data);

        if( *count ) {
            *l = lsmStringListAlloc(0);
            g_hash_table_iter_init(&iter, op->data);
            while(g_hash_table_iter_next(&iter, &key, &value)) {
                if(LSM_ERR_OK != lsmStringListAppend(*l, (char *)key) ) {
                    *count = 0;
                    lsmStringListFree(*l);
                    *l = NULL;
                    return LSM_ERR_NO_MEMORY;
                }
            }
        }
        return LSM_ERR_OK;
    }
    return LSM_ERR_INVALID_OPTIONAL_DATA;
}

const char *lsmOptionalDataStringGet(lsmOptionalData *op,
                                                    const char *key)
{
    if( LSM_IS_OPTIONAL_DATA(op) ) {
        return (const char*)g_hash_table_lookup(op->data, key);
    }
    return NULL;
}

int lsmOptionalDataStringSet(lsmOptionalData *op,
                                                const char *key,
                                                const char *value)
{
    if( LSM_IS_OPTIONAL_DATA(op) ) {
        char *k_value = strdup(key);
        char *d_value = strdup(value);

        if( k_value && d_value ) {
            g_hash_table_remove(op->data, (gpointer)k_value);
            g_hash_table_insert(op->data, (gpointer)k_value, (gpointer)d_value);
            return LSM_ERR_OK;
        } else {
            free(k_value);
            free(d_value);
            return LSM_ERR_NO_MEMORY;
        }
    }
    return LSM_ERR_INVALID_OPTIONAL_DATA;
}

#ifdef  __cplusplus
}
#endif
