<?php
// $Id$

require_once 'dmemcache.inc';

/** Implementation of cache.inc with memcache logic included **/

/**
 * Return data from the persistent cache.
 *
 * @param $key
 *   The cache ID of the data to retrieve.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache' for the default cache.
 */
function cache_get($key, $table = 'cache') {
  return dmemcache_get($key, $table);
}

/**
 * Store data in memcache.
 *
 * @param $cid
 *   The cache ID of the data to store.
 * @param $table
 *   The table $table to store the data in. Valid core values are 'cache_filter',
 *   'cache_menu', 'cache_page', or 'cache'.
 * @param $data
 *   The data to store in the cache. Complex data types must be serialized first.
 * @param $expire
 *   One of the following values:
 *   - CACHE_PERMANENT: Indicates that the item should never be removed unless
 *     explicitly told to using cache_clear_all() with a cache ID.
 *   - CACHE_TEMPORARY: Indicates that the item should be removed at the next
 *     general cache wipe.
 *   - A Unix timestamp: Indicates that the item should be kept at least until
 *     the given time, after which it behaves like CACHE_TEMPORARY.
 * @param $headers
 *   A string containing HTTP header information for cached pages.
 * @param $db_storage
 *   This boolean is unique to the memcache.inc implementation of cache set.
 *   It allows us to do a cache_set and not write to the database, but only
 *   to memcache.
 */
function cache_set($cid, $table = 'cache', $data, $expire = CACHE_PERMANENT, $headers = NULL) {
  // Create new cache object.
  $cache = new stdClass;
  $cache->cid = $cid;
  $cache->data = is_object($data) ? memcache_clone($data) : $data;
  $cache->created = time();
  $cache->expire = $expire;
  $cache->headers = $headers;

  // Save to memcache
  if ($expire == CACHE_PERMANENT || $expire > time()) {
    dmemcache_set($cid, $cache, $expire, $table);
  }
  else if ($expire == CACHE_TEMPORARY) {
    // A compromise for CACHE_TEMPORARY: Cache for three minutes.
    dmemcache_set($cid, $cache, 180, $table);
  }
}

/**
 *
 * Expire data from the cache. If called without arguments, expirable
 * entries will be cleared from the cache_page table.
 *
 * @param $cid
 *   If set, the cache ID to delete. Otherwise, all cache entries that can
 *   expire are deleted.
 *
 * @param $table
 *   If set, the table $table to delete from. Mandatory
 *   argument if $cid is set.
 *
 * @param $wildcard
 *   If set to TRUE, the $cid is treated as a substring
 *   to match rather than a complete ID. The match is a right hand
 *   match. If '*' is given as $cid, the table $table will be emptied.
 */
function cache_clear_all($cid = NULL, $table = NULL, $wildcard = FALSE) {
  // Memcache logic is simpler because memcache doesn't have a minimum cache
  // lifetime consideration (it handles it internally), and doesn't support
  // wildcards.
  $bin = empty($table) ? 'cache' : $table;
  if (empty($cid) || ($cid == '*' && $wildcard !== TRUE)) {
    # don't do anything if cid is unset. this matches the default drupal behavior...
    if ($wildcard && $cid != '*') {
      if (variable_get('memcache_debug', FALSE)) {
        // call watchdog, since you probably didn't want to flush the entire bin.
        watchdog('memcache', "illegal wildcard in cache_clear_all - not flushing entire bin. table: $table. cid: $cid", WATCHDOG_WARNING);
      }
    }
  }
  else if ($cid == '*' || $wildcard === TRUE) {
    dmemcache_flush($table);
  }
  else {
    dmemcache_delete($cid, $table);
  }
}

/**
 * Provide a substitute clone() function for PHP4. This is a copy of drupal_clone
 * because common.inc isn't included early enough in the bootstrap process to
 * be able to depend on drupal_clone.
 */
function memcache_clone($object) {
  return version_compare(phpversion(), '5.0') < 0 ? $object : clone($object);
}
