// Expected return: 42
// #1282: a single function with more labels than codegen's internal
// label table could hold used to hard-error ("codegen: too many labels"),
// surfaced by the self-hosting spike (a huge NodeKind/opcode-switch
// function somewhere in cccc's own source). CG_MAX_LABELS/
// CG_MAX_LABEL_PATCHES (src/codegen_call.c) were raised 256/1024 ->
// 4096/16384; this generates a function with several hundred goto labels
// to exercise the raised ceiling.
int main(void) {
    int sum = 0;
    goto l0;
l0:
    sum += 0;
    goto l1;
l1:
    sum += 1;
    goto l2;
l2:
    sum += 2;
    goto l3;
l3:
    sum += 3;
    goto l4;
l4:
    sum += 4;
    goto l5;
l5:
    sum += 5;
    goto l6;
l6:
    sum += 6;
    goto l7;
l7:
    sum += 0;
    goto l8;
l8:
    sum += 1;
    goto l9;
l9:
    sum += 2;
    goto l10;
l10:
    sum += 3;
    goto l11;
l11:
    sum += 4;
    goto l12;
l12:
    sum += 5;
    goto l13;
l13:
    sum += 6;
    goto l14;
l14:
    sum += 0;
    goto l15;
l15:
    sum += 1;
    goto l16;
l16:
    sum += 2;
    goto l17;
l17:
    sum += 3;
    goto l18;
l18:
    sum += 4;
    goto l19;
l19:
    sum += 5;
    goto l20;
l20:
    sum += 6;
    goto l21;
l21:
    sum += 0;
    goto l22;
l22:
    sum += 1;
    goto l23;
l23:
    sum += 2;
    goto l24;
l24:
    sum += 3;
    goto l25;
l25:
    sum += 4;
    goto l26;
l26:
    sum += 5;
    goto l27;
l27:
    sum += 6;
    goto l28;
l28:
    sum += 0;
    goto l29;
l29:
    sum += 1;
    goto l30;
l30:
    sum += 2;
    goto l31;
l31:
    sum += 3;
    goto l32;
l32:
    sum += 4;
    goto l33;
l33:
    sum += 5;
    goto l34;
l34:
    sum += 6;
    goto l35;
l35:
    sum += 0;
    goto l36;
l36:
    sum += 1;
    goto l37;
l37:
    sum += 2;
    goto l38;
l38:
    sum += 3;
    goto l39;
l39:
    sum += 4;
    goto l40;
l40:
    sum += 5;
    goto l41;
l41:
    sum += 6;
    goto l42;
l42:
    sum += 0;
    goto l43;
l43:
    sum += 1;
    goto l44;
l44:
    sum += 2;
    goto l45;
l45:
    sum += 3;
    goto l46;
l46:
    sum += 4;
    goto l47;
l47:
    sum += 5;
    goto l48;
l48:
    sum += 6;
    goto l49;
l49:
    sum += 0;
    goto l50;
l50:
    sum += 1;
    goto l51;
l51:
    sum += 2;
    goto l52;
l52:
    sum += 3;
    goto l53;
l53:
    sum += 4;
    goto l54;
l54:
    sum += 5;
    goto l55;
l55:
    sum += 6;
    goto l56;
l56:
    sum += 0;
    goto l57;
l57:
    sum += 1;
    goto l58;
l58:
    sum += 2;
    goto l59;
l59:
    sum += 3;
    goto l60;
l60:
    sum += 4;
    goto l61;
l61:
    sum += 5;
    goto l62;
l62:
    sum += 6;
    goto l63;
l63:
    sum += 0;
    goto l64;
l64:
    sum += 1;
    goto l65;
l65:
    sum += 2;
    goto l66;
l66:
    sum += 3;
    goto l67;
l67:
    sum += 4;
    goto l68;
l68:
    sum += 5;
    goto l69;
l69:
    sum += 6;
    goto l70;
l70:
    sum += 0;
    goto l71;
l71:
    sum += 1;
    goto l72;
l72:
    sum += 2;
    goto l73;
l73:
    sum += 3;
    goto l74;
l74:
    sum += 4;
    goto l75;
l75:
    sum += 5;
    goto l76;
l76:
    sum += 6;
    goto l77;
l77:
    sum += 0;
    goto l78;
l78:
    sum += 1;
    goto l79;
l79:
    sum += 2;
    goto l80;
l80:
    sum += 3;
    goto l81;
l81:
    sum += 4;
    goto l82;
l82:
    sum += 5;
    goto l83;
l83:
    sum += 6;
    goto l84;
l84:
    sum += 0;
    goto l85;
l85:
    sum += 1;
    goto l86;
l86:
    sum += 2;
    goto l87;
l87:
    sum += 3;
    goto l88;
l88:
    sum += 4;
    goto l89;
l89:
    sum += 5;
    goto l90;
l90:
    sum += 6;
    goto l91;
l91:
    sum += 0;
    goto l92;
l92:
    sum += 1;
    goto l93;
l93:
    sum += 2;
    goto l94;
l94:
    sum += 3;
    goto l95;
l95:
    sum += 4;
    goto l96;
l96:
    sum += 5;
    goto l97;
l97:
    sum += 6;
    goto l98;
l98:
    sum += 0;
    goto l99;
l99:
    sum += 1;
    goto l100;
l100:
    sum += 2;
    goto l101;
l101:
    sum += 3;
    goto l102;
l102:
    sum += 4;
    goto l103;
l103:
    sum += 5;
    goto l104;
l104:
    sum += 6;
    goto l105;
l105:
    sum += 0;
    goto l106;
l106:
    sum += 1;
    goto l107;
l107:
    sum += 2;
    goto l108;
l108:
    sum += 3;
    goto l109;
l109:
    sum += 4;
    goto l110;
l110:
    sum += 5;
    goto l111;
l111:
    sum += 6;
    goto l112;
l112:
    sum += 0;
    goto l113;
l113:
    sum += 1;
    goto l114;
l114:
    sum += 2;
    goto l115;
l115:
    sum += 3;
    goto l116;
l116:
    sum += 4;
    goto l117;
l117:
    sum += 5;
    goto l118;
l118:
    sum += 6;
    goto l119;
l119:
    sum += 0;
    goto l120;
l120:
    sum += 1;
    goto l121;
l121:
    sum += 2;
    goto l122;
l122:
    sum += 3;
    goto l123;
l123:
    sum += 4;
    goto l124;
l124:
    sum += 5;
    goto l125;
l125:
    sum += 6;
    goto l126;
l126:
    sum += 0;
    goto l127;
l127:
    sum += 1;
    goto l128;
l128:
    sum += 2;
    goto l129;
l129:
    sum += 3;
    goto l130;
l130:
    sum += 4;
    goto l131;
l131:
    sum += 5;
    goto l132;
l132:
    sum += 6;
    goto l133;
l133:
    sum += 0;
    goto l134;
l134:
    sum += 1;
    goto l135;
l135:
    sum += 2;
    goto l136;
l136:
    sum += 3;
    goto l137;
l137:
    sum += 4;
    goto l138;
l138:
    sum += 5;
    goto l139;
l139:
    sum += 6;
    goto l140;
l140:
    sum += 0;
    goto l141;
l141:
    sum += 1;
    goto l142;
l142:
    sum += 2;
    goto l143;
l143:
    sum += 3;
    goto l144;
l144:
    sum += 4;
    goto l145;
l145:
    sum += 5;
    goto l146;
l146:
    sum += 6;
    goto l147;
l147:
    sum += 0;
    goto l148;
l148:
    sum += 1;
    goto l149;
l149:
    sum += 2;
    goto l150;
l150:
    sum += 3;
    goto l151;
l151:
    sum += 4;
    goto l152;
l152:
    sum += 5;
    goto l153;
l153:
    sum += 6;
    goto l154;
l154:
    sum += 0;
    goto l155;
l155:
    sum += 1;
    goto l156;
l156:
    sum += 2;
    goto l157;
l157:
    sum += 3;
    goto l158;
l158:
    sum += 4;
    goto l159;
l159:
    sum += 5;
    goto l160;
l160:
    sum += 6;
    goto l161;
l161:
    sum += 0;
    goto l162;
l162:
    sum += 1;
    goto l163;
l163:
    sum += 2;
    goto l164;
l164:
    sum += 3;
    goto l165;
l165:
    sum += 4;
    goto l166;
l166:
    sum += 5;
    goto l167;
l167:
    sum += 6;
    goto l168;
l168:
    sum += 0;
    goto l169;
l169:
    sum += 1;
    goto l170;
l170:
    sum += 2;
    goto l171;
l171:
    sum += 3;
    goto l172;
l172:
    sum += 4;
    goto l173;
l173:
    sum += 5;
    goto l174;
l174:
    sum += 6;
    goto l175;
l175:
    sum += 0;
    goto l176;
l176:
    sum += 1;
    goto l177;
l177:
    sum += 2;
    goto l178;
l178:
    sum += 3;
    goto l179;
l179:
    sum += 4;
    goto l180;
l180:
    sum += 5;
    goto l181;
l181:
    sum += 6;
    goto l182;
l182:
    sum += 0;
    goto l183;
l183:
    sum += 1;
    goto l184;
l184:
    sum += 2;
    goto l185;
l185:
    sum += 3;
    goto l186;
l186:
    sum += 4;
    goto l187;
l187:
    sum += 5;
    goto l188;
l188:
    sum += 6;
    goto l189;
l189:
    sum += 0;
    goto l190;
l190:
    sum += 1;
    goto l191;
l191:
    sum += 2;
    goto l192;
l192:
    sum += 3;
    goto l193;
l193:
    sum += 4;
    goto l194;
l194:
    sum += 5;
    goto l195;
l195:
    sum += 6;
    goto l196;
l196:
    sum += 0;
    goto l197;
l197:
    sum += 1;
    goto l198;
l198:
    sum += 2;
    goto l199;
l199:
    sum += 3;
    goto l200;
l200:
    sum += 4;
    goto l201;
l201:
    sum += 5;
    goto l202;
l202:
    sum += 6;
    goto l203;
l203:
    sum += 0;
    goto l204;
l204:
    sum += 1;
    goto l205;
l205:
    sum += 2;
    goto l206;
l206:
    sum += 3;
    goto l207;
l207:
    sum += 4;
    goto l208;
l208:
    sum += 5;
    goto l209;
l209:
    sum += 6;
    goto l210;
l210:
    sum += 0;
    goto l211;
l211:
    sum += 1;
    goto l212;
l212:
    sum += 2;
    goto l213;
l213:
    sum += 3;
    goto l214;
l214:
    sum += 4;
    goto l215;
l215:
    sum += 5;
    goto l216;
l216:
    sum += 6;
    goto l217;
l217:
    sum += 0;
    goto l218;
l218:
    sum += 1;
    goto l219;
l219:
    sum += 2;
    goto l220;
l220:
    sum += 3;
    goto l221;
l221:
    sum += 4;
    goto l222;
l222:
    sum += 5;
    goto l223;
l223:
    sum += 6;
    goto l224;
l224:
    sum += 0;
    goto l225;
l225:
    sum += 1;
    goto l226;
l226:
    sum += 2;
    goto l227;
l227:
    sum += 3;
    goto l228;
l228:
    sum += 4;
    goto l229;
l229:
    sum += 5;
    goto l230;
l230:
    sum += 6;
    goto l231;
l231:
    sum += 0;
    goto l232;
l232:
    sum += 1;
    goto l233;
l233:
    sum += 2;
    goto l234;
l234:
    sum += 3;
    goto l235;
l235:
    sum += 4;
    goto l236;
l236:
    sum += 5;
    goto l237;
l237:
    sum += 6;
    goto l238;
l238:
    sum += 0;
    goto l239;
l239:
    sum += 1;
    goto l240;
l240:
    sum += 2;
    goto l241;
l241:
    sum += 3;
    goto l242;
l242:
    sum += 4;
    goto l243;
l243:
    sum += 5;
    goto l244;
l244:
    sum += 6;
    goto l245;
l245:
    sum += 0;
    goto l246;
l246:
    sum += 1;
    goto l247;
l247:
    sum += 2;
    goto l248;
l248:
    sum += 3;
    goto l249;
l249:
    sum += 4;
    goto l250;
l250:
    sum += 5;
    goto l251;
l251:
    sum += 6;
    goto l252;
l252:
    sum += 0;
    goto l253;
l253:
    sum += 1;
    goto l254;
l254:
    sum += 2;
    goto l255;
l255:
    sum += 3;
    goto l256;
l256:
    sum += 4;
    goto l257;
l257:
    sum += 5;
    goto l258;
l258:
    sum += 6;
    goto l259;
l259:
    sum += 0;
    goto l260;
l260:
    sum += 1;
    goto l261;
l261:
    sum += 2;
    goto l262;
l262:
    sum += 3;
    goto l263;
l263:
    sum += 4;
    goto l264;
l264:
    sum += 5;
    goto l265;
l265:
    sum += 6;
    goto l266;
l266:
    sum += 0;
    goto l267;
l267:
    sum += 1;
    goto l268;
l268:
    sum += 2;
    goto l269;
l269:
    sum += 3;
    goto l270;
l270:
    sum += 4;
    goto l271;
l271:
    sum += 5;
    goto l272;
l272:
    sum += 6;
    goto l273;
l273:
    sum += 0;
    goto l274;
l274:
    sum += 1;
    goto l275;
l275:
    sum += 2;
    goto l276;
l276:
    sum += 3;
    goto l277;
l277:
    sum += 4;
    goto l278;
l278:
    sum += 5;
    goto l279;
l279:
    sum += 6;
    goto l280;
l280:
    sum += 0;
    goto l281;
l281:
    sum += 1;
    goto l282;
l282:
    sum += 2;
    goto l283;
l283:
    sum += 3;
    goto l284;
l284:
    sum += 4;
    goto l285;
l285:
    sum += 5;
    goto l286;
l286:
    sum += 6;
    goto l287;
l287:
    sum += 0;
    goto l288;
l288:
    sum += 1;
    goto l289;
l289:
    sum += 2;
    goto l290;
l290:
    sum += 3;
    goto l291;
l291:
    sum += 4;
    goto l292;
l292:
    sum += 5;
    goto l293;
l293:
    sum += 6;
    goto l294;
l294:
    sum += 0;
    goto l295;
l295:
    sum += 1;
    goto l296;
l296:
    sum += 2;
    goto l297;
l297:
    sum += 3;
    goto l298;
l298:
    sum += 4;
    goto l299;
l299:
    sum += 5;
    goto l300;
l300:
    sum += 6;
    goto l301;
l301:
    sum += 0;
    goto l302;
l302:
    sum += 1;
    goto l303;
l303:
    sum += 2;
    goto l304;
l304:
    sum += 3;
    goto l305;
l305:
    sum += 4;
    goto l306;
l306:
    sum += 5;
    goto l307;
l307:
    sum += 6;
    goto l308;
l308:
    sum += 0;
    goto l309;
l309:
    sum += 1;
    goto l310;
l310:
    sum += 2;
    goto l311;
l311:
    sum += 3;
    goto l312;
l312:
    sum += 4;
    goto l313;
l313:
    sum += 5;
    goto l314;
l314:
    sum += 6;
    goto l315;
l315:
    sum += 0;
    goto l316;
l316:
    sum += 1;
    goto l317;
l317:
    sum += 2;
    goto l318;
l318:
    sum += 3;
    goto l319;
l319:
    sum += 4;
    goto l320;
l320:
    sum += 5;
    goto l321;
l321:
    sum += 6;
    goto l322;
l322:
    sum += 0;
    goto l323;
l323:
    sum += 1;
    goto l324;
l324:
    sum += 2;
    goto l325;
l325:
    sum += 3;
    goto l326;
l326:
    sum += 4;
    goto l327;
l327:
    sum += 5;
    goto l328;
l328:
    sum += 6;
    goto l329;
l329:
    sum += 0;
    goto l330;
l330:
    sum += 1;
    goto l331;
l331:
    sum += 2;
    goto l332;
l332:
    sum += 3;
    goto l333;
l333:
    sum += 4;
    goto l334;
l334:
    sum += 5;
    goto l335;
l335:
    sum += 6;
    goto l336;
l336:
    sum += 0;
    goto l337;
l337:
    sum += 1;
    goto l338;
l338:
    sum += 2;
    goto l339;
l339:
    sum += 3;
    goto l340;
l340:
    sum += 4;
    goto l341;
l341:
    sum += 5;
    goto l342;
l342:
    sum += 6;
    goto l343;
l343:
    sum += 0;
    goto l344;
l344:
    sum += 1;
    goto l345;
l345:
    sum += 2;
    goto l346;
l346:
    sum += 3;
    goto l347;
l347:
    sum += 4;
    goto l348;
l348:
    sum += 5;
    goto l349;
l349:
    sum += 6;
    goto l350;
l350:
    sum += 0;
    goto l351;
l351:
    sum += 1;
    goto l352;
l352:
    sum += 2;
    goto l353;
l353:
    sum += 3;
    goto l354;
l354:
    sum += 4;
    goto l355;
l355:
    sum += 5;
    goto l356;
l356:
    sum += 6;
    goto l357;
l357:
    sum += 0;
    goto l358;
l358:
    sum += 1;
    goto l359;
l359:
    sum += 2;
    goto l360;
l360:
    sum += 3;
    goto l361;
l361:
    sum += 4;
    goto l362;
l362:
    sum += 5;
    goto l363;
l363:
    sum += 6;
    goto l364;
l364:
    sum += 0;
    goto l365;
l365:
    sum += 1;
    goto l366;
l366:
    sum += 2;
    goto l367;
l367:
    sum += 3;
    goto l368;
l368:
    sum += 4;
    goto l369;
l369:
    sum += 5;
    goto l370;
l370:
    sum += 6;
    goto l371;
l371:
    sum += 0;
    goto l372;
l372:
    sum += 1;
    goto l373;
l373:
    sum += 2;
    goto l374;
l374:
    sum += 3;
    goto l375;
l375:
    sum += 4;
    goto l376;
l376:
    sum += 5;
    goto l377;
l377:
    sum += 6;
    goto l378;
l378:
    sum += 0;
    goto l379;
l379:
    sum += 1;
    goto l380;
l380:
    sum += 2;
    goto l381;
l381:
    sum += 3;
    goto l382;
l382:
    sum += 4;
    goto l383;
l383:
    sum += 5;
    goto l384;
l384:
    sum += 6;
    goto l385;
l385:
    sum += 0;
    goto l386;
l386:
    sum += 1;
    goto l387;
l387:
    sum += 2;
    goto l388;
l388:
    sum += 3;
    goto l389;
l389:
    sum += 4;
    goto l390;
l390:
    sum += 5;
    goto l391;
l391:
    sum += 6;
    goto l392;
l392:
    sum += 0;
    goto l393;
l393:
    sum += 1;
    goto l394;
l394:
    sum += 2;
    goto l395;
l395:
    sum += 3;
    goto l396;
l396:
    sum += 4;
    goto l397;
l397:
    sum += 5;
    goto l398;
l398:
    sum += 6;
    goto l399;
l399:
    sum += 0;
    goto l400;
l400:
    sum += 1;
    goto l401;
l401:
    sum += 2;
    goto l402;
l402:
    sum += 3;
    goto l403;
l403:
    sum += 4;
    goto l404;
l404:
    sum += 5;
    goto l405;
l405:
    sum += 6;
    goto l406;
l406:
    sum += 0;
    goto l407;
l407:
    sum += 1;
    goto l408;
l408:
    sum += 2;
    goto l409;
l409:
    sum += 3;
    goto l410;
l410:
    sum += 4;
    goto l411;
l411:
    sum += 5;
    goto l412;
l412:
    sum += 6;
    goto l413;
l413:
    sum += 0;
    goto l414;
l414:
    sum += 1;
    goto l415;
l415:
    sum += 2;
    goto l416;
l416:
    sum += 3;
    goto l417;
l417:
    sum += 4;
    goto l418;
l418:
    sum += 5;
    goto l419;
l419:
    sum += 6;
    goto l420;
l420:
    sum += 0;
    goto l421;
l421:
    sum += 1;
    goto l422;
l422:
    sum += 2;
    goto l423;
l423:
    sum += 3;
    goto l424;
l424:
    sum += 4;
    goto l425;
l425:
    sum += 5;
    goto l426;
l426:
    sum += 6;
    goto l427;
l427:
    sum += 0;
    goto l428;
l428:
    sum += 1;
    goto l429;
l429:
    sum += 2;
    goto l430;
l430:
    sum += 3;
    goto l431;
l431:
    sum += 4;
    goto l432;
l432:
    sum += 5;
    goto l433;
l433:
    sum += 6;
    goto l434;
l434:
    sum += 0;
    goto l435;
l435:
    sum += 1;
    goto l436;
l436:
    sum += 2;
    goto l437;
l437:
    sum += 3;
    goto l438;
l438:
    sum += 4;
    goto l439;
l439:
    sum += 5;
    goto l440;
l440:
    sum += 6;
    goto l441;
l441:
    sum += 0;
    goto l442;
l442:
    sum += 1;
    goto l443;
l443:
    sum += 2;
    goto l444;
l444:
    sum += 3;
    goto l445;
l445:
    sum += 4;
    goto l446;
l446:
    sum += 5;
    goto l447;
l447:
    sum += 6;
    goto l448;
l448:
    sum += 0;
    goto l449;
l449:
    sum += 1;
    goto l450;
l450:
    sum += 2;
    goto l451;
l451:
    sum += 3;
    goto l452;
l452:
    sum += 4;
    goto l453;
l453:
    sum += 5;
    goto l454;
l454:
    sum += 6;
    goto l455;
l455:
    sum += 0;
    goto l456;
l456:
    sum += 1;
    goto l457;
l457:
    sum += 2;
    goto l458;
l458:
    sum += 3;
    goto l459;
l459:
    sum += 4;
    goto l460;
l460:
    sum += 5;
    goto l461;
l461:
    sum += 6;
    goto l462;
l462:
    sum += 0;
    goto l463;
l463:
    sum += 1;
    goto l464;
l464:
    sum += 2;
    goto l465;
l465:
    sum += 3;
    goto l466;
l466:
    sum += 4;
    goto l467;
l467:
    sum += 5;
    goto l468;
l468:
    sum += 6;
    goto l469;
l469:
    sum += 0;
    goto l470;
l470:
    sum += 1;
    goto l471;
l471:
    sum += 2;
    goto l472;
l472:
    sum += 3;
    goto l473;
l473:
    sum += 4;
    goto l474;
l474:
    sum += 5;
    goto l475;
l475:
    sum += 6;
    goto l476;
l476:
    sum += 0;
    goto l477;
l477:
    sum += 1;
    goto l478;
l478:
    sum += 2;
    goto l479;
l479:
    sum += 3;
    goto l480;
l480:
    sum += 4;
    goto l481;
l481:
    sum += 5;
    goto l482;
l482:
    sum += 6;
    goto l483;
l483:
    sum += 0;
    goto l484;
l484:
    sum += 1;
    goto l485;
l485:
    sum += 2;
    goto l486;
l486:
    sum += 3;
    goto l487;
l487:
    sum += 4;
    goto l488;
l488:
    sum += 5;
    goto l489;
l489:
    sum += 6;
    goto l490;
l490:
    sum += 0;
    goto l491;
l491:
    sum += 1;
    goto l492;
l492:
    sum += 2;
    goto l493;
l493:
    sum += 3;
    goto l494;
l494:
    sum += 4;
    goto l495;
l495:
    sum += 5;
    goto l496;
l496:
    sum += 6;
    goto l497;
l497:
    sum += 0;
    goto l498;
l498:
    sum += 1;
    goto l499;
l499:
    sum += 2;
    if (sum != 1494)
        return 1;
    return 42;
}
