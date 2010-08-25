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
    $cache = dmemcache_get($cid, $this->bin, $this->memcache);
    return $this->valid($cid, $cache) ? $cache : FALSE;
  }

  function getMultiple(&$cids) {
    $results = dmemcache_get_multi($cids, $this->bin, $this->memcache);
    foreach ($results as $cid => $result) {
      if (!$this->valid($result->cid, $result)) {
        // This object has expired, so don't return it.
        unset($results[$cid]);
      }
      else {
        // Remove items from the referenced $cids array that we are returning,
        // per the comment in cache_get_multiple() in includes/cache.inc.
        unset($cids[$result->cid]);
      }
    }
    return $results;
  }

  protected function valid($cid, $cache) {
    if (!is_object($cache)) {
      return FALSE;
    }

    if (!$this->wildcard_valid($cid, $cache)) {
      return FALSE;
    }

    // Determine when the current bin was last flushed.
    $cache_flush = variable_get("cache_flush_$this->bin", 0);

    $cache_lifetime = variable_get('cache_lifetime', 0);
    $item_flushed_globally = $cache->created && $cache_flush && $cache_lifetime && ($cache->created < min($cache_flush, time() - $cache_lifetime));

    $cache_bins = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;

    $item_flushed_for_user = is_array($cache_bins) && isset($cache_bins[$this->bin]) && ($cache->created < $cache_bins[$this->bin]);
    if ($item_flushed_for_user) {
      return FALSE;
    }

    // The item can be expired if:
    // - A liftetime is set and the item is older than both the lifetime and
    //   the global flush.
    // - The item has been create before the bin was flushed for this user.
    // - The item could simply expire.
    //
    // For the two global cases we try and grab a lock.  If we get the lock, we
    // return FALSE instead of the cached object which should cause it to be
    // rebuilt.  If we do not get the lock, we return the cached object despite
    // it's expired  The goal here is to avoid cache stampedes.
    // By default the cache stampede semaphore is held for 15 seconds.  This
    // can be adjusted by setting the memcache_stampede_semaphore variable.
    // TODO: Can we log when a sempahore expires versus being intentionally
    // freed to track when this is happening?
    $item_expired = isset($cache->expire) && $cache->expire !== CACHE_PERMANENT && $cache->expire <= time();
    return !(($item_flushed_globally || $item_expired) && dmemcache_add($cid .'_semaphore', '', variable_get('memcache_stampede_semaphore', 15), $this->bin));
  }

  function set($cid, $data, $expire = CACHE_PERMANENT, array $headers = NULL) {
    $created = time();

    // Create new cache object.
    $cache = new stdClass;
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = $created;
    $cache->headers = $headers;
    // Record the previous number of wildcard flushes affecting our cid.
    $cache->flushes = $this->wildcard_flushes($cid);
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

    if (empty($cid) || $wildcard === TRUE) {
      if ($cid == '*') {
        $cid = '';
      }
      if (variable_get('cache_lifetime', 0) && empty($cid)) {
        // Update the timestamp of the last global flushing of this bin.  When
        // retrieving data from this bin, we will compare the cache creation
        // time minus the cache_flush time to the cache_lifetime to determine
        // whether or not the cached item is still valid.
        variable_set("cache_flush_$this->bin", time());

        // We store the time in the current user's session which is saved into
        // the sessions table by sess_write().  We then simulate that the cache
        // was flushed for this user by not returning cached data to this user
        // that was cached before the timestamp.
        if (isset($_SESSION['cache_flush']) && is_array($_SESSION['cache_flush'])) {
          $cache_bins = $_SESSION['cache_flush'];
        }
        else {
          $cache_bins = array();
        }
        $cache_bins[$this->bin] = time();
        $_SESSION['cache_flush'] = $cache_bins;
      }
      else {
        // Register a wildcard flush for current cid
        $this->wildcards($cid, TRUE);
      }
    }
    else {
      $cids = is_array($cid) ? $cid : array($cid);
      foreach ($cids as $cid) {
        dmemcache_delete($cid, $this->bin, $this->memcache);
      }
    }
  }

  /**
   * We hash cids to keep them a consistent, managable length.  Alternative algorithms
   * can be specified if you're looking for better performance (benchmark first!).
   * Hash collissions are not a big deal, simply leads to all collided items being
   * flushed together.
   */
  function hash_cid($cid) {
    static $hashes = array();
    $memcache_hash = variable_get('memacache_hash', 'md5');
    $hashes[$cid] = $memcache_hash($cid);
    return $hashes[$cid];
  }

  /**
   * Determine all possible hashes that could match our cid.  We optimize away
   * the overhead of checking all possible matches by using multiget.
   */
  private function multihash_cid($cid) {
    static $hashes = array();
    if (!isset($hashes[$cid])) {
      for ($i = 0; $i <= strlen($cid); $i++) {
        $subcid = substr($cid, 0, $i);
        $hashes[$cid][$subcid] = '.wildcard-'. $this->bin . $this->hash_cid($subcid);
      }
    }
    return $hashes[$cid];
  }

  /**
   * Sum of all matching wildcards.  Checking any single cache item's flush value
   * against this single-value sum tells us whether or not a new wildcard flush
   * has affected the cached item.
   */
  private function wildcard_flushes($cid) {
    return array_sum($this->wildcards($cid));
  }

  /**
   * Utilize multiget to retrieve all possible wildcard matches, storing statically
   * so multiple cache requests for the same item on the same page load doesn't add
   * overhead.
   */
  private function wildcards($cid, $flush = FALSE) {
    static $wildcards = array();
    if (!isset($wildcard[$this->bin]) || !isset($wildcards[$this->bin][$cid])) {
      $multihash = $this->multihash_cid($cid);
      $wildcards[$this->bin][$cid] = dmemcache_get_multi($multihash, $this->bin);
      if (!is_array($wildcards[$this->bin][$cid])) {
        $wildcards[$this->bin][$cid] = array();
      }
    }
    if ($flush) {
      $hash = $this->hash_cid($cid);
      $wildcard = dmemcache_key('.wildcard-' . $this->bin . $hash, $this->bin);
      if (isset($wildcards[$this->bin][$cid][$wildcard])) {
        $mc = dmemcache_object($this->bin);
        $mc->increment($wildcard);
        $wildcards[$this->bin][$cid][$wildcard]++;
      }
      else {
        $wildcards[$this->bin][$cid][$wildcard] = 1;
        dmemcache_set('.wildcard-' . $this->bin . $hash, 1, 0, $this->bin);
      }
    }
    return $wildcards[$this->bin][$cid];
  }

  /**
   * Check if a wildcard flush has invalidated the current cached copy.
   */
  private function wildcard_valid($cid, $cache) {
    if ((int)$cache->flushes < (int)$this->wildcard_flushes($cid)) {
      return FALSE;
    }
    return TRUE;
  }

  function isEmpty() {
    // We do not know so err on the safe side?
    return FALSE;
  }
}

