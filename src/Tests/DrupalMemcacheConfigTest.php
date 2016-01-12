<?php

/**
 * @file
 * Contains \Drupal\memcache\Tests\DrupalMemcacheConfigTest.
 */

namespace Drupal\memcache\Tests;

use Drupal\memcache\DrupalMemcacheConfig;
use Drupal\Core\Site\Settings;
use Drupal\Tests\UnitTestCase;

/**
 * @coversDefaultClass \Drupal\memcache\DrupalMemcacheConfig
 * @group memcache
 */
class DrupalMemcacheConfigTest extends UnitTestCase {

  /**
   * Simple settings array to test against.
   *
   * @var array
   */
  protected $config = array();

  /**
   * The class under test.
   *
   * @var \Drupal\memcache\DrupalMemcacheConfig
   */
  protected $settings;

  /**
   * @covers ::__construct
   */
  protected function setUp(){
    $this->config = array(
      'memcache' => array(
        'servers' => array('127.0.0.2:12345' => 'default'),
        'bin' => array('default' => 'default')
      ),
      'hash_salt' => $this->randomMachineName(),
    );
    $settings = new Settings($this->config);
    $this->settings = new DrupalMemcacheConfig($settings);
  }

  /**
   * @covers ::get
   */
  public function testGet() {
    // Test stored settings.
    $this->assertEquals($this->config['memcache']['servers'], $this->settings->get('servers'), 'The correct setting was not returned.');
    $this->assertEquals($this->config['memcache']['bin'], $this->settings->get('bin'), 'The correct setting was not returned.');

    // Test retrieving settings via static methods
    $this->assertEquals($this->config['memcache']['servers'], DrupalMemcacheConfig::get('servers'), 'The correct setting was not returned.');
    $this->assertEquals($this->config['memcache']['bin'], DrupalMemcacheConfig::get('bin'), 'The correct setting was not returned.');

    // Test setting that isn't stored with default.
    $this->assertEquals('3', $this->settings->get('three', '3'), 'Default value for a setting not properly returned.');
    $this->assertNull($this->settings->get('nokey'), 'Non-null value returned for a setting that should not exist.');

    // Test setting that isn't stored with default using static methods.
    $this->assertEquals('4', DrupalMemcacheConfig::get('three', '4'), 'Default value for a setting not properly returned.');
    $this->assertNull(DrupalMemcacheConfig::get('nokey'), 'Non-null value returned for a setting that should not exist.');
  }

  /**
   * @covers ::getAll
   */
  public function testGetAll() {
    $this->assertEquals($this->config['memcache'], $this->settings->getAll());
    $this->assertEquals($this->config['memcache'], DrupalMemcacheConfig::getAll());
  }
}
