<?php
// $Id$

require_once 'dmemcache.inc';

/** Implementation of cache.inc with memcache logic included **/

class MemCacheDrupal implements DrupalCacheInterface {
  function __construct($bin) {
    $this->memcache = dmemcache_object($bin);
    $this->bin = $bin;
  }
  function get($cid) {
    // Determine when the current bin was last flushed.
    $cache_flush = variable_get("cache_flush_$this->bin", 0);
    // Retrieve the item from the cache.
    $cache = dmemcache_get($cid, $this->bin, $this->memcache);
    if (is_object($cache)) {
      $cache_bins = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;
      // Items cached before the cache was last flushed are no longer valid.
      $cache_lifetime = variable_get('cache_lifetime', 0);
      if ($cache_lifetime && $cache->created && $cache_flush &&
          ($cache->created < $cache_flush) &&
          ((time() - $cache->created >= $cache_lifetime)) ||
          (is_array($cache_bins) && $cache_bins[$this->bin] &&
          $cache_bins[$this->bin] > $cache->created)) {
        // Cache item expired, return NULL.
        return FALSE;
      }
      return $cache;
    }
    return FALSE;
  }

  function getMultiple(&$cids) {
    // With cache flushes and stampede protection it's impossible to do use
    // the multiple get. Pity.
    $results = array();
    foreach ($cids as $key => $cid) {
      if ($result = $this->get($cid)) {
        $results[$cid] = $result;
        unset($cids[$key]);
      }
    }
    return $results;
  }

  function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    $created = time();

    // Create new cache object.
    $cache = new stdClass;
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = $created;
    $cache->headers = $headers;
    if ($expire == CACHE_TEMPORARY) {
      // Convert CACHE_TEMPORARY (-1) into something that will live in memcache
      // until the next flush.
      $cache->expire = time() + 2591999;
    }
    // Expire time is in seconds if less than 30 days, otherwise is a timestamp.
    else if ($expire != CACHE_PERMANENT && $expire < 2592000) {
      // Expire is expressed in seconds, convert to the proper future timestamp
      // as expected in dmemcache_get().
      $cache->expire = time() + $expire;
    }
    else {
      $cache->expire = $expire;
    }

    // We manually track the expire time in $cache->expire.  When the object
    // expires, we only allow one request to rebuild it to avoid cache stampedes.
    // Other requests for the expired object while it is still being rebuilt get
    // the expired object.
    dmemcache_set($cid, $cache, 0, $this->bin, $this->memcache);
  }

  function clear($cid = NULL, $wildcard = FALSE) {
    // Default behavior for when cache_clear_all() is called without parameters
    // is to clear all of the expirable entries in the block and page caches.
    if (empty($cid) || ($cid == '*' && $wildcard !== TRUE)) {
      // don't do anything if cid is unset. this matches the default drupal behavior...
      if ($wildcard && $cid != '*') {
        if (variable_get('memcache_debug', FALSE)) {
          // call watchdog, since you probably didn't want to flush the entire bin.
          watchdog('memcache', "illegal wildcard in cache_clear_all - not flushing entire bin. bin: $this->bin. cid: $cid", WATCHDOG_WARNING);
        }
      }
    }
    else if ($cid == '*' || $wildcard === TRUE) {
      if (variable_get('cache_lifetime', 0)) {
        // Update the timestamp of the last global flushing of this bin.  When
        // retrieving data from this bin, we will compare the cache creation
        // time minus the cache_flush time to the cache_lifetime to determine
        // whether or not the cached item is still valid.
        variable_set("cache_flush_$this->bin", time());

        // We store the time in the current user's session which is saved into
        // the sessions table by sess_write().  We then simulate that the cache
        // was flushed for this user by not returning cached data to this user
        // that was cached before the timestamp.
        if (is_array($_SESSION['cache_flush'])) {
          $cache_bins = $_SESSION['cache_flush'];
        }
        else {
          $cache_bins = array();
        }
        $cache_bins[$this->bin] = time();
        $_SESSION['cache_flush'] = $cache_bins;
      }
      else {
        dmemcache_flush($this->bin, $this->memcache);
      }
    }
    else {
      $cids = is_array($cid) ? $cid : array($cid);
      foreach ($cids as $cid) {
        dmemcache_delete($cid, $this->bin, $this->memcache);
      }
    }
  }
  
  function isEmpty() {
    // We do not know so err on the safe side?
    return FALSE;
  }
}

