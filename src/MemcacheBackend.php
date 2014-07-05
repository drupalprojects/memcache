<?php

/**
 * @file
 * Contains \Drupal\memcache\MemcacheBackend.
 */

namespace Drupal\memcache;

use Drupal\Core\Site\Settings;
use Drupal\Core\Cache\CacheBackendInterface;
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
   * The lock count.
   *
   * @var int
   */
  protected $lockCount = 0;

  /**
   * The memcache wrapper object.
   *
   * @var \Drupal\memcache\DrupalMemcacheInterface
   */
  protected $memcache;

  /**
   * The lock backend that should be used.
   *
   * @var \Drupal\Core\Lock\LockBackendInterface
   */
  protected $lock;

  /**
   * The Settings instance.
   *
   * @var \Drupal\Component\Utility\Settings
   */
  protected $settings;

  public function __construct($bin, DrupalMemcacheInterface $memcache, LockBackendInterface $lock, Settings $settings) {
    $this->bin = $bin;
    $this->memcache = $memcache;
    $this->lock = $lock;
    $this->settings = $settings;
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
    $cache = $this->memcache->getMulti($cids);
    foreach ($cache as $cid => $result) {
      if (!$this->valid($cid, $result) && !$allow_invalid) {
        // This object has expired, so don't return it.
        unset($cache[$cid]);
      }
    }

    // Remove items from the referenced $cids array that we are returning,
    // per comment in Drupal\Core\Cache\CacheBackendInterface::getMultiple().
    $cids = array_diff($cids, array_keys($cache));

    return $cache;
  }

  /**
   * {@inheritdoc}
   */
  protected function valid($cid, $cache) {
    $lock_key = "memcache_$cid:$this->bin";

    if ($cache) {
      // Items that have expired are invalid.
      if (isset($cache->expire) && ($cache->expire !== CacheBackendInterface::CACHE_PERMANENT) && ($cache->expire <= REQUEST_TIME)) {
        // If the memcache_stampede_protection variable is set, allow one
        // process to rebuild the cache entry while serving expired content to
        // the rest.
        if ($this->settings->get('memcache_stampede_protection', FALSE)) {
          // The process that acquires the lock will get a cache miss, all
          // others will get a cache hit.
          if ($this->lock->acquire($lock_key, $this->settings->get('memcache_stampede_semaphore', 15))) {
            $cache->valid = FALSE;
          }
        }
        else {
          $cache->valid = FALSE;
        }
      }
      else {
        $cache->valid = TRUE;
      }
    }
    // On cache misses, attempt to avoid stampedes when the
    // memcache_stampede_protection variable is enabled.
    else {
      if ($this->settings->get('memcache_stampede_protection', FALSE) && !$this->lock->acquire($lock_key, $this->settings->get('memcache_stampede_semaphore', 15))) {
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
        if ($this->lockCount <= $this->settings->get('memcache_stampede_wait_limit', 3)) {
          // The memcache_stampede_semaphore variable was used in previous
          // releases of memcache, but the max_wait variable was not, so by
          // default divide the semaphore value by 3 (5 seconds).
         $this->lock->wait($lock_key, $this->settings->get('memcache_stampede_wait_time', 5));
          $cache = $this->get($cid);
        }
      }
    }

    return (bool) $cache->valid;
  }

  /**
   * {@inheritdoc}
   */
  public function set($cid, $data, $expire = CacheBackendInterface::CACHE_PERMANENT, array $tags = array()) {
    // Create new cache object.
    $cache = new \stdClass();
    $cache->cid = $cid;
    $cache->data = is_object($data) ? clone $data : $data;
    $cache->created = REQUEST_TIME;

    // Expire time is in seconds if less than 30 days, otherwise is a timestamp.
    if ($expire != CacheBackendInterface::CACHE_PERMANENT && ($expire < 2592000)) {
      // Expire is expressed in seconds, convert to the proper future timestamp
      // as expected in DrupalMemcache::set().
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
    if (($cache->expire == CacheBackendInterface::CACHE_PERMANENT)) {
      $memcache_expire = $cache->expire;
    }
    elseif (($cache->expire < REQUEST_TIME)) {
      $memcache_expire = CacheBackendInterface::CACHE_PERMANENT;
    }
    else {
      $memcache_expire = $cache->expire + ($cache->expire - REQUEST_TIME * 2);
    }

    return $this->memcache->set($cid, $cache, $memcache_expire);
  }

  /**
   * {@inheritdoc}
   */
  public function setMultiple(array $items) {
    foreach ($items as $cid => $item) {
      $item += array(
        'expire' => CacheBackendInterface::CACHE_PERMANENT,
        'tags' => array(),
      );
      $this->set($cid, $item['data'], $item['expire'], $item['tags']);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function delete($cid) {
    $this->memcache->delete($cid);
  }

  /**
   * {@inheritdoc}
   */
  public function deleteMultiple(array $cids) {
    foreach ($cids as $cid) {
      $this->memcache->delete($cid);
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
      $this->memcache->delete($cid);
    }
  }

  /**
   * {@inheritdoc}
   */
  public function invalidateAll() {
    $this->memcache->flush();
  }

  /**
   * {@inheritdoc}
   */
  public function deleteTags(array $tags) {
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
    // We do not know so err on the safe side? Not sure if we can know this?
    return TRUE;
  }

}
