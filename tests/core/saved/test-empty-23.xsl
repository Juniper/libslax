<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:variable name="aaa-temp-1">
    <aa1>
      <aaa>AAA</aaa>
    </aa1>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="aaa" select="slax-ext:node-set($aaa-temp-1)"/>
  <xsl:variable name="bbb-temp-2">
    <bb1>
      <bbb>BBB</bbb>
    </bb1>
    <bb2>
      <bbb>BBB</bbb>
    </bb2>
    <bb3>
      <bbb>BBB</bbb>
    </bb3>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="bbb" select="slax-ext:node-set($bbb-temp-2)"/>
  <xsl:variable name="ccc-temp-3">
    <cc1>
      <ccc>CCC</ccc>
    </cc1>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="ccc" select="slax-ext:node-set($ccc-temp-3)"/>
  <xsl:variable name="slax-ternary-1">
    <xsl:choose>
      <xsl:when test="$aaa">
        <xsl:copy-of select="$bbb"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:copy-of select="$ccc"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="ddd" select="slax:value($slax-ternary-1)"/>
  <xsl:param name="action" select="&quot;fight&quot;"/>
  <xsl:variable name="slax-ternary-2">
    <xsl:choose>
      <xsl:when test="$action = &quot;fight&quot;">
        <xsl:copy-of select="$bbb"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:copy-of select="$ccc"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:param xmlns:slax="http://code.google.com/p/libslax/slax" name="how" select="slax:value($slax-ternary-2)"/>
  <xsl:variable name="slax-ternary-3">
    <xsl:choose>
      <xsl:when test="$how = &quot;quick&quot;">
        <xsl:copy-of select="1"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:copy-of select="10"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:param xmlns:slax="http://code.google.com/p/libslax/slax" name="count" select="slax:value($slax-ternary-3)"/>
  <xsl:variable name="slax-ternary-4">
    <xsl:choose>
      <xsl:when test="this">
        <xsl:copy-of select="that"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:copy-of select="the-other"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="top" select="slax:value($slax-ternary-4)"/>
  <xsl:variable name="data-temp-4">
    <more>
      <a>
        <xsl:value-of select="1"/>
      </a>
      <b>
        <xsl:value-of select="2"/>
      </b>
      <c>
        <xsl:value-of select="3"/>
      </c>
      <d>
        <xsl:value-of select="4"/>
      </d>
      <e>
        <xsl:value-of select="5"/>
      </e>
      <h>
        <xsl:value-of select="6"/>
      </h>
      <k>k</k>
      <n>n</n>
      <o>
        <xsl:value-of select="7"/>
      </o>
      <p>
        <xsl:value-of select="8"/>
      </p>
      <x>
        <xsl:value-of select="9"/>
      </x>
      <z>
        <xsl:value-of select="10"/>
      </z>
    </more>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="data" select="slax-ext:node-set($data-temp-4)"/>
  <xsl:template match="/">
    <top>
      <d>
        <xsl:copy-of select="$ddd"/>
      </d>
      <d2>
        <xsl:variable name="slax-ternary-5">
          <xsl:choose>
            <xsl:when test="$aaa">
              <xsl:copy-of select="$bbb"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:copy-of select="$ccc"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:copy-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-5)"/>
      </d2>
      <action>
        <xsl:value-of select="$action"/>
      </action>
      <how>
        <xsl:value-of select="$how"/>
      </how>
      <count>
        <xsl:value-of select="$count"/>
      </count>
      <xsl:for-each select="$data/more">
        <xsl:variable name="slax-ternary-6">
          <xsl:choose>
            <xsl:when test="a">
              <xsl:copy-of select="b"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:copy-of select="c"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="one" select="slax:value($slax-ternary-6)"/>
        <xsl:variable name="slax-ternary-7">
          <xsl:variable name="slax-ternary-7-cond" select="d"/>
          <xsl:choose>
            <xsl:when test="$slax-ternary-7-cond">
              <xsl:copy-of select="$slax-ternary-7-cond"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:copy-of select="e"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="two" select="slax:value($slax-ternary-7)"/>
        <xsl:variable name="slax-ternary-10">
          <xsl:choose>
            <xsl:when test="f">
              <xsl:copy-of select="1"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:variable name="slax-ternary-9">
                <xsl:choose>
                  <xsl:when test="g">
                    <xsl:copy-of select="2"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:variable name="slax-ternary-8">
                      <xsl:choose>
                        <xsl:when test="h">
                          <xsl:copy-of select="3"/>
                        </xsl:when>
                        <xsl:otherwise>
                          <xsl:copy-of select="4"/>
                        </xsl:otherwise>
                      </xsl:choose>
                    </xsl:variable>
                    <xsl:copy-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-8)"/>
                  </xsl:otherwise>
                </xsl:choose>
              </xsl:variable>
              <xsl:copy-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-9)"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="three" select="slax:value($slax-ternary-10)"/>
        <xsl:variable name="slax-ternary-13">
          <xsl:variable name="slax-ternary-13-cond" select="i"/>
          <xsl:choose>
            <xsl:when test="$slax-ternary-13-cond">
              <xsl:copy-of select="$slax-ternary-13-cond"/>
            </xsl:when>
            <xsl:otherwise>
              <xsl:variable name="slax-ternary-12">
                <xsl:variable name="slax-ternary-12-cond" select="j"/>
                <xsl:choose>
                  <xsl:when test="$slax-ternary-12-cond">
                    <xsl:copy-of select="$slax-ternary-12-cond"/>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:variable name="slax-ternary-11">
                      <xsl:variable name="slax-ternary-11-cond" select="k"/>
                      <xsl:choose>
                        <xsl:when test="$slax-ternary-11-cond">
                          <xsl:copy-of select="$slax-ternary-11-cond"/>
                        </xsl:when>
                        <xsl:otherwise>
                          <xsl:copy-of select="l"/>
                        </xsl:otherwise>
                      </xsl:choose>
                    </xsl:variable>
                    <xsl:copy-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-11)"/>
                  </xsl:otherwise>
                </xsl:choose>
              </xsl:variable>
              <xsl:copy-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-12)"/>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:variable>
        <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="four" select="slax:value($slax-ternary-13)"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <two>
          <xsl:value-of select="$two"/>
        </two>
        <three>
          <xsl:value-of select="$three"/>
        </three>
        <four>
          <xsl:value-of select="$four"/>
        </four>
        <fish>
          <xsl:variable name="slax-ternary-14">
            <xsl:choose>
              <xsl:when test="m">
                <xsl:copy-of select="n"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:copy-of select="o"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-14)"/>
        </fish>
        <fish>
          <xsl:variable name="slax-ternary-15">
            <xsl:choose>
              <xsl:when test="p">
                <xsl:copy-of select="5"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:copy-of select="6"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:call-template name="fish">
            <xsl:with-param xmlns:slax="http://code.google.com/p/libslax/slax" name="name" select="slax:value($slax-ternary-15)"/>
          </xsl:call-template>
        </fish>
        <contains>
          <xsl:variable name="slax-ternary-16">
            <xsl:choose>
              <xsl:when test="$one">
                <xsl:copy-of select="$two"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:copy-of select="$three"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:variable name="slax-ternary-17">
            <xsl:choose>
              <xsl:when test="$one">
                <xsl:copy-of select="$two"/>
              </xsl:when>
              <xsl:otherwise>
                <xsl:copy-of select="$three"/>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="contains(slax:value($slax-ternary-16), slax:value($slax-ternary-17))"/>
        </contains>
      </xsl:for-each>
    </top>
  </xsl:template>
  <xsl:template name="fish">
    <xsl:param name="slax-ternary-18">
      <xsl:choose>
        <xsl:when test="x">
          <xsl:copy-of select="y"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:copy-of select="z"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:param>
    <xsl:param xmlns:slax="http://code.google.com/p/libslax/slax" name="name" select="slax:value($slax-ternary-18)"/>
    <xsl:variable name="slax-ternary-19">
      <xsl:choose>
        <xsl:when test="$name">
          <xsl:copy-of select="concat($name, &quot;00&quot;)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:copy-of select="20"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:value-of xmlns:slax="http://code.google.com/p/libslax/slax" select="slax:value($slax-ternary-19)"/>
  </xsl:template>
</xsl:stylesheet>
