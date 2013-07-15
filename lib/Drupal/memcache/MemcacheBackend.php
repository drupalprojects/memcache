<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheBackend.
 */

namespace Drupal\memcache;

use Drupal\Component\Utility\Settings;
use Drupal\Core\Cache\CacheBackendInterface;
use Drupal\Core\KeyValueStore\KeyValueStoreInterface;
use Drupal\Core\Lock\LockBackendInterface;

/**
 * Defines a Memcache cache backend.
 */
class MemcacheBackend implements CacheBackendInterface {

  /**
   * The cache bin to use.
   *
   * @var string
   */
  protected $bin;

  /**
   * @todo
   *
   * @var int
   */
  protected $lockCount = 0;

  /**
   * The lock backend that should be used.
   *
   * @var \Drupal\Core\Lock\LockBackendInterface
   */
  protected $lock;

  /**
   * The Settings instance.
   *
   * @var array|\Drupal\Component\Utility\Settings
   */
  protected $settings;

  /**
   * @todo
   *
   * @var \Drupal\Core\Lock\LockBackendInterface
   */
  protected $state;

  public function __construct($bin, LockBackendInterface $lock, Settings $settings, KeyValueStoreInterface $state) {
    $this->bin = $bin;
    $this->lock = $lock;
    $this->settings = $settings;
    $this->state = $state;
    $this->memcache = DrupalMemcache::getObject($bin);
  }

  /**
   * {@inheritdoc}
   */
  public function get($cid, $allow_invalid = FALSE) {
    $cids = array($cid);
    $cache = $this->getMultiple($cids, $allow_invalid);
    return reset($cache);
  }

  /**
   * {@inheritdoc}
   */
  public function getMultiple(&$cids, $allow_invalid = FALSE) {
    $cache = DrupalMemcache::getMulti($cids, $this->bin, $this->memcache);

    if (!$allow_invalid) {
      foreach ($cache as $cid => $result) {
        if (!$this->valid($cid, $result)) {
          // This object has expired, so don't return it.
          unset($cache[$cid]);
        }
      }
    }

    // Remove items from the referenced $cids array that we are returning,
    // per the comment in cache_get_multiple() in Drupal\Core\Cache\CacheBackendInterface
    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  protected function valid($cid, $cache) {
    if ($cache) {
      // Items that have expired are invalid.
      if (isset($cache->expire) && ($cache->expire !== CacheBackendInterface::CACHE_PERMANENT) && ($cache->expire <= REQUEST_TIME)) {
        // If the memcache_stampede_protection variable is set, allow one process
        // to rebuild the cache entry while serving expired content to the
        // rest. Note that core happily returns expired cache items as valid and
        // relies on cron to expire them, but this is mostly reliant on its
        // use of CACHE_TEMPORARY which does not map well to memcache.
        // @see http://drupal.org/node/534092
        if ($this->settings->get('memcache_stampede_protection', FALSE)) {
          // The process that acquires the lock will get a cache miss, all
          // others will get a cache hit.
          if ($this->lock->acquire("memcache_$cid:$this->bin", $this->settings->get('memcache_stampede_semaphore', 15))) {
            $cache = FALSE;
          }
        }
        else {
          $cache = FALSE;
        }
      }
    }

    // On cache misses, attempt to avoid stampedes when the
    // memcache_stampede_protection variable is enabled.
    if (!$cache) {
      if ($this->settings->get('memcache_stampede_protection', FALSE) && !$this->lock->acquire("memcache_$cid:$this->bin", $this->settings->get('memcache_stampede_semaphore', 15))) {
        // Prevent any single request from waiting more than three times due to
        // stampede protection. By default this is a maximum total wait of 15
        // seconds. This accounts for two possibilities - a cache and lock miss
        // more than once for the same item. Or a cache and lock miss for
        // different items during the same request.
        // @todo: it would be better to base this on time waited rather than
        // number of waits, but the lock API does not currently provide this
        // information. Currently the limit will kick in for three waits of 25ms
        // or three waits of 5000ms.
        $this->lockCount++;
        if ($$this->lockCount <= $this->settings->get('memcache_stampede_wait_limit', 3)) {
          // The memcache_stampede_semaphore variable was used in previous releases
          // of memcache, but the max_wait variable was not, so by default divide
          // the semaphore value by 3 (5 seconds).
         $this->lock->wait("memcache_$cid:$this->bin", $this->settings->get('memcache_stampede_wait_time', 5));
          $cache = $this->get($cid);
        }
      }
    }

    return (bool) $cache;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CacheBackendInterface::CACHE_PERMANENT, array $tags = array()) {
    // Create new cache object.
    $cache = new \stdClass();
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = time();

    // Expire time is in seconds if less than 30 days, otherwise is a timestamp.
    if ($expire != CacheBackendInterface::CACHE_PERMANENT && $expire < 2592000) {
      // Expire is expressed in seconds, convert to the proper future timestamp
      // as expected in dmemcache_get().
      $cache->expire = REQUEST_TIME + $expire;
    }
    else {
      $cache->expire = $expire;
    }

    // Manually track the expire time in $cache->expire.  When the object
    // expires, if stampede protection is enabled, it may be served while one
    // process rebuilds it. The ttl sent to memcache is set to the expire twice
    // as long into the future, this allows old items to be expired by memcache
    // rather than evicted along with a sufficient period for stampede
    // protection to continue to work.
    if ($cache->expire == CacheBackendInterface::CACHE_PERMANENT) {
      $memcache_expire = $cache->expire;
    }
    else {
      $memcache_expire = $cache->expire + (($cache->expire - REQUEST_TIME) * 2);
    }

    return DrupalMemcache::set($cid, $cache, $memcache_expire, $this->bin, $this->memcache);
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid) {
    DrupalMemcache::delete($cid, $this->bin, $this->memcache);
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids) {
    foreach ($cids as $cid) {
      DrupalMemcache::delete($cid, $this->bin, $this->memcache);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function deleteAll() {
    // Invalidate all keys, as we can't actually delete all?
    $this->invalidateAll();
  }


  /**
   * {@inheritdoc}
   */
  public function deleteTags(array $tags) {
  }

  /**
   * {@inheritdoc}
   */
  public function invalidate($cid) {
    $this->invalidateMultiple((array) $cid);
  }

  /**
   * Marks cache items as invalid.
   *
   * Invalid items may be returned in later calls to get(), if the $allow_invalid
   * argument is TRUE.
   *
   * @param string $cids
   *   An array of cache IDs to invalidate.
   *
   * @see Drupal\Core\Cache\CacheBackendInterface::deleteMultiple()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidate()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateTags()
   * @see Drupal\Core\Cache\CacheBackendInterface::invalidateAll()
   */
  public function invalidateMultiple(array $cids) {
    // @todo implement deleteMulti instead?
    foreach ($cids as $cid) {
      DrupalMemcache::delete($cid, $this->bin, $this->memcache);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function invalidateAll() {
    DrupalMemcache::flush($this->bin);
  }

  /**
   * {@inheritdoc}
   */
  public function invalidateTags(array $tags) {
    // TODO: Implement invalidateTags() method.
  }

  /**
   * {@inheritdoc}
   */
  public function removeBin() {
    // Do nothing here too?
  }

  /**
   * {@inheritdoc}
   */
  public function garbageCollection() {
    // Memcache will invalidate items; That items memory allocation is then
    // freed up and reused. So nothing needs to be deleted/cleaned up here.
  }

  /**
   * (@inheritdoc)
   */
  public function isEmpty() {
    // We do not know so err on the safe side?
    return FALSE;
  }

}
