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
    global $memcached_prefixes, $memcached_counters;
    if (!isset($memcached_prefixes)) {
      $memcached_prefixes = array();
    }
    if (!isset($memcached_counters)) {
      $memcached_counters = array();
    }
    // Determine when the current bin was last flushed.
    $cache_flush = variable_get("cache_flush_$this->bin", 0);
    // Retrieve the item from the cache.
    $cache = dmemcache_get($cid, $this->bin, $this->memcache);
    if (is_object($cache)) {
      // Load the prefix directory.
      if (!isset($memcached_prefixes[$this->bin])) {
        $memcached_prefixes[$this->bin] = dmemcache_get('.prefixes', $this->bin);
        if ($memcached_prefixes[$this->bin] === FALSE) {
          $memcached_prefixes[$this->bin] = array();
        }
        $memcached_counters[$this->bin] = array();
      }

      // Check if the item being fetched matches any prefixes.
      foreach ($memcached_prefixes[$this->bin] as $prefix) {
        if (substr($cid, 0, strlen($prefix)) == $prefix) {
          // On a match, check if we already know the current counter value.
          if (!isset($memcached_counters[$this->bin][$prefix])) {
            $memcached_counters[$this->bin][$prefix] = dmemcache_get('.prefix.' . $prefix, $this->bin);
          }

          // If a matching prefix for this item was cleared after storing it, it is invalid.
          if (!isset($cache->counters[$prefix]) || $cache->counters[$prefix] < $memcached_counters[$this->bin][$prefix]) {
            return 0;
          }
        }
      }
      $cache_bins = isset($_SESSION['cache_flush']) ? $_SESSION['cache_flush'] : NULL;
      // Items cached before the cache was last flushed are no longer valid.
      $cache_lifetime = variable_get('cache_lifetime', 0);
      if ($cache_lifetime && $cache->created && $cache_flush &&
          ($cache->created < $cache_flush) &&
          ((time() - $cache->created >= $cache_lifetime)) ||
          (isset($cache_bins) && is_array($cache_bins) &&
          isset($cache_bins[$this->bin]) &&
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
    global $memcached_prefixes, $memcached_counters;
    $created = time();

    if (!isset($memcached_prefixes[$this->bin])) {
      $memcached_prefixes[$this->bin] = dmemcache_get('.prefixes', $this->bin);
      if ($memcached_prefixes[$this->bin] === FALSE) {
        $memcached_prefixes[$this->bin] = array();
      }
      $memcached_counters[$this->bin] = array();
    }

    $counters = array();
    // Check if the item being stored matches any prefixes.
    foreach ($memcached_prefixes[$this->bin] as $prefix) {
      if (substr($cid, 0, strlen($prefix)) == $prefix) {
        // On a match, check if we already know the current counter value.
        if (!isset($memcached_counters[$this->bin][$prefix])) {
          $memcached_counters[$this->bin][$prefix] = dmemcache_get('.prefix.' . $prefix, $this->bin);
        }
        $counters[$prefix] = $memcached_counters[$this->bin][$prefix];
      }
    }

    // Create new cache object.
    $cache = new stdClass;
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = $created;
    $cache->headers = $headers;
    $cache->counters = $counters;
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
    global $memcached_prefixes, $memcached_counters;

    if (empty($cid) || $wildcard === TRUE) {
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
        if ($cid == '*') {
          $cid = '';
        }

        // Get a memcached object for complex operations.
        $mc = dmemcache_object($this->bin);

        // Load the prefix directory.
        if (!isset($memcached_prefixes[$this->bin])) {
          $memcached_prefixes[$this->bin] = dmemcache_get('.prefixes', $this->bin);
          if ($memcached_prefixes[$this->bin] === FALSE) {
            $memcached_prefixes[$this->bin] = array();
          }
        }

        // Ensure the prefix being cleared is listed, if not, atomically add it.
        // Adding new prefixes should be rare.
        if (!in_array($cid, $memcached_prefixes[$this->bin])) {
          // Acquire a semaphore.
          $lock_key = dmemcache_key('.prefixes.lock', $this->bin);
          while (!$mc->add($lock_key, 1, FALSE, 10)) {
            usleep(1000);
          }

          // Get a fresh copy of the prefix directory.
          $memcached_prefixes[$this->bin] = dmemcache_get('.prefixes', $this->bin);
          if ($memcached_prefixes[$this->bin] === FALSE) {
            $memcached_prefixes[$this->bin] = array();
          }

          // Only add the prefix if it's not in the updated directory.
          if (!in_array($cid, $memcached_prefixes[$this->bin])) {
            // Add the new prefix.
            $memcached_prefixes[$this->bin][] = $cid;

            // Store to memcached.
            dmemcache_set('.prefixes', $memcached_prefixes[$this->bin], 0, $this->bin);

            // Set the clearing counter to zero.
            dmemcache_set('.prefix.' . $cid, 0, 0, $this->bin);
          }

          // Release the semaphore.
          dmemcache_delete('.prefixes.lock', $this->bin);
        }

        // Increment the prefix clearing counter.
        $counter_key = dmemcache_key('.prefix.' . $cid, $this->bin);
        $memcached_counters[$this->bin][$cid] = $mc->increment($counter_key);
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

